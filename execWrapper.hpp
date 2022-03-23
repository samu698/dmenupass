#pragma once

#include <string>
#include <vector>
#include <array>
#include <streambuf>
#include <iostream>
#include <initializer_list>

#include <sys/wait.h>
#include <cstring>
#include <unistd.h>

class iopipes : std::streambuf, public std::iostream {
	union PipeFds {
		struct { int readEnd, writeEnd; };
		int fds[2];

		PipeFds() { pipe(fds); }
		void close() { ::close(readEnd); ::close(writeEnd); }
		~PipeFds() { close(); }
	} ipipe, opipe;

	using traits = std::streambuf::traits_type;
	char buffer[1024];

public:
	iopipes() : std::iostream(this) { setg(buffer, buffer, buffer); }

	void closeWrite() { opipe.close(); }
	void closeRead() { ipipe.close(); }
	void close() { closeRead(); closeWrite(); }
	void closeUnneded() { ::close(ipipe.writeEnd); ::close(opipe.readEnd); }
	void dup() {
		dup2(ipipe.writeEnd, STDOUT_FILENO);
		dup2(opipe.readEnd, STDIN_FILENO);
		close();
	}
protected:
	virtual traits::int_type overflow(traits::int_type c) {
		if (c == traits::eof()) return traits::eof();
		if (::write(opipe.writeEnd, &c, 1) != 1) return traits::eof();
		return c;
	}
	virtual std::streamsize xsputn(const char* data, std::streamsize len) {
		ssize_t writeret = ::write(opipe.writeEnd, data, len);
		return writeret == -1 ? traits::eof() : writeret;
	}

	virtual int underflow() {
		if (gptr() == egptr()) {
			ssize_t readCount = ::read(ipipe.readEnd, gptr(), sizeof buffer);
			if (readCount <= 0) return traits::eof();
			setg(buffer, buffer, buffer + readCount);
		}
		return traits::to_int_type(*gptr());
	}
};

class StringList {
	std::vector<std::string> strings;
	std::vector<const char*> cstrings;
public:
	StringList(const std::vector<std::string>& args) : strings(args) {
		cstrings.reserve(strings.size() + 1);
		for (int i = 0; i < strings.size(); i++) cstrings.push_back(strings[i].c_str());
		cstrings.push_back(nullptr);
	}

	const char** tocarray() { return cstrings.data(); }
};

class Process {
	std::string file;
	StringList argv;
	int pid;
	iopipes pipes;
public:
	Process(const std::string& file, const std::vector<std::string>& args) : file(file), argv(args) {}

	void run() {
		int exitCode;
		pid = fork();
		if (pid == 0) {
			pipes.dup();
			int ret = execvp(file.c_str(), (char**)argv.tocarray());
			exit(1);
		}
		pipes.closeUnneded();
	}

	iopipes& stream() {
		return pipes;
	}

	int join() {
		int exitCode;
		pipes.close();
		waitpid(pid, &exitCode, 0);
		return exitCode;
	}
};
