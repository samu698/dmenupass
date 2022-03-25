#include "passwordStore.hpp"
#include "passwordGenerator.hpp"
#include "XClipboard.hpp"
#include "dmenu.hpp"
#include "notifications.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <random>
#include <optional>
#include <functional>
#include <stdexcept>

using namespace std::literals;

const int maxLines = 20;
const DmenuFlags defaultFlags = { .showPos = DmenuFlags::CENTER };

constexpr static auto generators = passwordGeneratorList(
	[] { return "!-~"sv; },
	[] { return "0-9A-Za-z!?+_()"sv; }
);

using namespace std::placeholders;

PasswordStore passwordStore;
XClipboard clipboard;
Notifications notifier("passDmenu");

template<typename T>
struct DmenuResult {
	std::string value, flags;
	std::optional<T> entry;

	DmenuResult(const std::string& inValue, const std::vector<T>& entries, std::function<bool(const std::string&, const T&)> predicate) {
		auto slashPos = std::find(begin(inValue), end(inValue), '/');
		value = std::string(begin(inValue), slashPos);
		if (slashPos != end(inValue)) flags = std::string(slashPos, end(inValue));

		auto entryIt = std::find_if(begin(entries), end(entries), std::bind(predicate, value, _1));
		if (entryIt != end(entries)) entry = *entryIt;
	}

	bool isEmpty() { return value.empty() && flags.empty(); }
	bool isCommand() { return !entry || !flags.empty(); }

	T& operator*() { return *entry; }
	T* operator->() { return &*entry; }
};

DmenuResult<std::vector<PasswordEntry>> askService(std::vector<std::vector<PasswordEntry>>& services) {
	std::vector<std::string> serviceOptions(services.size());
	std::transform(begin(services), end(services), begin(serviceOptions), [](const auto& service){  return service[0].service; });

	DmenuFlags flags = defaultFlags;
	flags.lines = std::min((int)services.size(), maxLines);
	Dmenu d(serviceOptions, flags);

	return DmenuResult<std::vector<PasswordEntry>>(d.result(), services, [](const auto& needle, const auto& v) { return v[0].service == needle; });
}

DmenuResult<PasswordEntry> askUser(std::vector<PasswordEntry> users) {
	std::vector<std::string> userOptions(users.size());
	std::transform(begin(users), end(users), begin(userOptions), [](const auto& entry){ return entry.username; });

	DmenuFlags flags = defaultFlags;
	flags.lines = std::min((int)users.size(), maxLines);
	flags.prompt = "User:";
	Dmenu d(userOptions, flags);

	return DmenuResult<PasswordEntry>(d.result(), users, [](const auto& needle, const auto& v) { return v.username == needle; });
}

bool askYesNo(std::string prompt, std::string yesOption = "Yes", std::string noOption = "No") {
	DmenuFlags flags = defaultFlags;
	flags.lines = 2;
	if (!prompt.empty()) flags.prompt = prompt;
	Dmenu d({ yesOption, noOption }, flags);

	return d.result() == yesOption;
}
std::string askValue(std::string prompt) {
	DmenuFlags flags = defaultFlags;
	flags.lines = 2;
	if (!prompt.empty()) flags.prompt = prompt;
	Dmenu d({}, flags);
	return d.result();
}
std::string askPassword(std::string prompt) {
	std::mt19937 rng(std::random_device{}());
	std::vector<std::string> suggestions(generators.size());
	std::transform(begin(generators), end(generators), begin(suggestions), [&rng](const auto& gen){ return gen(rng, 10); });

	DmenuFlags flags = defaultFlags;
	flags.lines = 2;
	if (!prompt.empty()) flags.prompt = prompt;
	Dmenu d(suggestions, flags);
	return d.result();
}

int handleUserCommand(const std::string& service, DmenuResult<PasswordEntry>& result) {
	if (result.flags.empty()) {
		if (!askYesNo("Do you want to:", "Add " + result.value + " to " + service, "Exit")) return EXIT_SUCCESS;
		
		std::string password = askPassword("Enter Password:");

		PasswordEntry newEntry(result.value, result.value, password);
		passwordStore.serializeEntry(newEntry, notifier);

		return EXIT_SUCCESS;
	}

	if (result.flags == "/e") {
		PasswordEntry toEdit = *result;
		passwordStore.decryptEntry(toEdit);
		toEdit.password = askPassword("New Password:");
		passwordStore.serializeEntry(toEdit, notifier);
		return EXIT_SUCCESS;
	}

	return EXIT_FAILURE;
}

int handleServiceCommand(DmenuResult<std::vector<PasswordEntry>>& result) {
	if (result.flags == "/e") {
		if (result->size() != 1) throw std::runtime_error("Cannot edit service directory");

		PasswordEntry toEdit = result->at(0);
		passwordStore.decryptEntry(toEdit);
		toEdit.password = askPassword("New Password:");
		passwordStore.serializeEntry(toEdit, notifier);
		return EXIT_SUCCESS;
	}

	if (result.flags == "/n" || result.flags.empty()) {
		std::string service;

		if (result.value.empty() || result.flags.empty()) {
			if (!askYesNo("Want to add service: ", "Yes", "No, exit program")) return EXIT_SUCCESS;	
			service = askValue("Enter service:");
		} else if (askYesNo("Want to add user: ", "Yes, Add to " + result.value, "No, exit program"))
			service = result.value;
		else 
			return EXIT_SUCCESS;
		
		std::string username = askValue("Enter Username:");
		if (username.empty()) return EXIT_FAILURE; // TODO: notify this

		std::string password = askPassword("Enter Password:");

		PasswordEntry newEntry(service, username, password);
		passwordStore.serializeEntry(newEntry, notifier);

		return EXIT_SUCCESS;
	}
	return EXIT_FAILURE;
}

void copyInfo(PasswordEntry& entry) {
	passwordStore.decryptEntry(entry);
	auto userNotification = notifier.create("Copied username", "Copied username for " + entry.service).timeout(5000).show();
	if (!clipboard.waitPaste(entry.username)) return;
	userNotification.clear();
	notifier.create("Copied password", "Copied password for " + entry.service).timeout(5000).show();
	clipboard.waitPaste(entry.password);
}

int main() {
	auto entries = passwordStore.getEntries();

	auto serviceResult = askService(entries);
	if (serviceResult.isEmpty()) return EXIT_SUCCESS;
	if (serviceResult.isCommand()) return handleServiceCommand(serviceResult);

	if (serviceResult->size() == 1) {
		copyInfo(serviceResult->at(0));
		return EXIT_SUCCESS;
	}

	auto userResult = askUser(*serviceResult);
	if (!userResult.isCommand()) {
		copyInfo(*userResult);
		return EXIT_SUCCESS;
	}
	if (userResult.isEmpty()) return EXIT_SUCCESS;
	return handleUserCommand(serviceResult.value, userResult);
}
