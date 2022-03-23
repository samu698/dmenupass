#pragma once

#include "execWrapper.hpp"
#include <vector>
#include <string>

struct DmenuFlags {
	enum ShowPos { TOP, BOTTOM, CENTER } showPos = TOP;
	bool caseInsensitive = false;
	int lines = -1;
	std::string prompt;

	std::vector<std::string> getFlagsVec() const {
		std::vector<std::string> flags = { "dmenu" };

		if (showPos == BOTTOM) flags.emplace_back("-b");
		else if (showPos == CENTER) flags.emplace_back("-c");

		if (caseInsensitive) flags.emplace_back("-i");

		if (lines > 0) {
			flags.emplace_back("-l");
			flags.emplace_back(std::to_string(lines));
		}

		if (!prompt.empty()) {
			flags.emplace_back("-p");
			flags.emplace_back(prompt);
		}

		return flags;
	}
};

class Dmenu {
	Process dmenuProcess;
	std::vector<std::string> options;
	bool done = false;
	std::string out;

	std::string run() {
		dmenuProcess.run();
		for (const auto& option : options)
			dmenuProcess.stream() << option << '\n';
		dmenuProcess.stream().closeWrite();

		std::string out;
		std::getline(dmenuProcess.stream(), out);
		dmenuProcess.join();
		return out;
	}
public:
	Dmenu(std::vector<std::string> options, const DmenuFlags& flags = {}) : dmenuProcess("dmenu", flags.getFlagsVec()), options(options) {}

	std::string result() {
		if (done) return out;

		out = run();
		done = true;
		return out;
	}
};
