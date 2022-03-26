#pragma once

#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <filesystem>
#include <iostream>

#include <gpgme.h>
#include "notifications.hpp"

using namespace std::placeholders;
namespace fs = std::filesystem;

struct PasswordEntry {
	fs::path path;
	std::string service, username, password;

	PasswordEntry(fs::path path) : path(path), service(path.stem()) {}
	PasswordEntry(fs::path path, std::string service) : path(path), service(service), username(path.stem()) {}
	PasswordEntry(std::string service, std::string username, std::string password) : service(service), username(username), password(password) {}

};

class PasswordStore {
	class GpgmeHandler {
		gpgme_ctx_t ctx;
		gpgme_key_t key;

		inline void check(gpgme_error_t error) {
			if (!error) return;
			std::string text = std::string(gpgme_strsource(error)) + ": " + gpgme_strerror(error) + '\n';
			throw std::runtime_error(text.c_str());
		}
	public:
		GpgmeHandler(std::string gpgId) {
			gpgme_check_version(nullptr);
			check(gpgme_engine_check_version(GPGME_PROTOCOL_OPENPGP));

			check(gpgme_new(&ctx));
			check(gpgme_ctx_set_engine_info(ctx, GPGME_PROTOCOL_OPENPGP, "/usr/bin/gpg", nullptr));

			check(gpgme_op_keylist_start(ctx, nullptr, 0));
			for (;;) {
				gpgme_error_t ret = gpgme_op_keylist_next(ctx, &key);
				if (gpg_err_code(ret) == GPG_ERR_EOF) throw std::runtime_error("Couldn't find the key");
				check(ret);

				// FIXME: probably this is wrong the gpgId could be something different than the email
				if (gpgId == key->uids->email) break;

				gpgme_key_release(key);
			}
		}
		~GpgmeHandler() { gpgme_release(ctx); }

		void encrypt(std::string content, fs::path path) {
			gpgme_data_t plain, chiper;
			gpgme_key_t keys[2] = { key, nullptr };

			check(gpgme_data_new_from_mem(&plain, content.c_str(), content.size(), 0));
			check(gpgme_data_new(&chiper));

			const auto flags = (gpgme_encrypt_flags_t)(GPGME_ENCRYPT_NO_ENCRYPT_TO | GPGME_ENCRYPT_NO_COMPRESS);
			check(gpgme_op_encrypt(ctx, keys, flags, plain, chiper));

			gpgme_data_release(plain);

			size_t length;
			char* chiperData = gpgme_data_release_and_get_mem(chiper, &length);

			std::ofstream file(path);
			file.write(chiperData, length);

			gpgme_free(chiperData);
		}

		std::string decrypt(fs::path path) {
			gpgme_data_t chiper, plain;
			check(gpgme_data_new_from_file(&chiper, path.c_str(), 1));
			check(gpgme_data_new(&plain));

			check(gpgme_op_decrypt(ctx, chiper, plain));

			gpgme_data_release(chiper);

			size_t length;
			char* plainData = gpgme_data_release_and_get_mem(plain, &length);

			std::string tmp(plainData, length);

			gpgme_free(plainData);

			return tmp;
		}
	};

	fs::path getStorePath() {
		char* env = getenv("PASSWORD_STORE_DIR");
		if (env) return env;
		if ((env = getenv("HOME"))) return fs::path(env) / ".password-store";
		throw std::runtime_error("Couldn't find password store path");
	}
	std::string readContents(fs::path path) {
		std::ifstream file(path);
		if (!file) throw std::runtime_error("Missing file");

		file.seekg(0, std::ios::end);
		// We don't read the last '\n'
		// TODO: find a better way to handle this
		size_t length = (size_t)file.tellg() - 1;
		file.seekg(0, std::ios::beg);

		std::string buffer(length, 0);
		file.read(buffer.data(), length);

		return buffer;
	}

	fs::path storePath;
	GpgmeHandler gpgme;
public:

	PasswordStore() : storePath(getStorePath()), gpgme(readContents(storePath/".gpg-id")) {}

	std::vector<std::vector<PasswordEntry>> getEntries() {
		std::vector<std::vector<PasswordEntry>> entries;

		auto usenameIterFunc = [](fs::path path){
			std::vector<PasswordEntry> entries;
			fs::directory_iterator folderIter(path);
			std::string service = path.filename();
			for (const auto& item : folderIter) {
				if (!item.is_regular_file() || item.path().extension() != ".gpg") continue;
				entries.emplace_back(item.path(), service);
			}
			return entries;
		};

		fs::directory_iterator folderIter(storePath);
		for (const auto& item : folderIter) {
			if (item.is_directory()) {
				if (item.path().filename() == ".git") continue;
				auto userEntries = usenameIterFunc(item.path());
				if (userEntries.size() > 0) entries.emplace_back(std::move(userEntries));
			} else {
				if (!item.is_regular_file() || item.path().extension() != ".gpg") continue;
				entries.push_back(std::vector<PasswordEntry>{ item.path() });
			}
		}
		return entries;
	}

	void decryptEntry(PasswordEntry& entry) {
		const auto lowerTrimmed = [](std::string str) {
			str.erase(begin(str), std::find_if_not(begin(str), end(str), isspace));
			std::transform(begin(str), end(str), begin(str), tolower);
			return str;
		};

		std::string line, infoStr = gpgme.decrypt(entry.path);
		std::istringstream strStream(infoStr);

		std::getline(strStream, entry.password);

		while (std::getline(strStream, line)) {
			std::string tocmp = lowerTrimmed(line);
			if (tocmp.find("username:") == 0 || tocmp.find("login:") == 0) {
				auto usernameBeg = begin(line) + line.find(":") + 1;
				usernameBeg = std::find_if_not(usernameBeg, line.end(), isspace);
				entry.username = std::string(usernameBeg, end(line));
				break;
			}
		}
	}

	void serializeEntry(const PasswordEntry& entry, Notifications notifier) {
		std::stringstream entryContent;
		entryContent << entry.password << '\n';
		entryContent << "username:" << entry.username << '\n';

		auto withGpgExtension = [](fs::path path){ path.concat(".gpg"); return path; };
		fs::path servicePath = storePath / entry.service;
		fs::path serviceFilePath = withGpgExtension(servicePath);

		if (fs::is_directory(servicePath)) {
			fs::path userFilePath = withGpgExtension(servicePath / entry.username);
			gpgme.encrypt(entryContent.str(), userFilePath);
			notifier.create("passDmenu", "Created directory service: " + userFilePath.native()).timeout(5000).show();
		} else {
			bool serviceFileExists = fs::exists(serviceFilePath);
			if (serviceFileExists) {
				PasswordEntry existingServiceFile(serviceFilePath);
				decryptEntry(existingServiceFile);
				if (existingServiceFile.username == entry.username) {
					gpgme.encrypt(entryContent.str(), serviceFilePath);
					notifier.create("passDmenu", "Modified service file: " + serviceFilePath.native()).timeout(5000).show();
				} else {
					fs::create_directory(servicePath);
					notifier.create("passDmenu", "Created folder: " + servicePath.native()).timeout(5000).show();

					const auto serviceFileNewPath = withGpgExtension(servicePath / existingServiceFile.username);
					fs::rename(serviceFilePath, serviceFileNewPath);
					notifier.create("passDmenu", "Moved service file to: " + serviceFileNewPath.native()).timeout(5000).show();

					const auto newUserFilePath = withGpgExtension(servicePath / entry.username);
					gpgme.encrypt(entryContent.str(), newUserFilePath);
					notifier.create("passDmenu", "Created user file: " + newUserFilePath.native()).timeout(5000).show();
				}
			} else {
				gpgme.encrypt(entryContent.str(), serviceFilePath);
				notifier.create("passDmenu", "Created service file: " + serviceFilePath.native()).timeout(5000).show();
			}
		}
	}
};
