#pragma once

#include <string>
#include <cstdint>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

class XClipboard {
	Display* dpy;
	Window root, win;
	const struct Atoms {
		Atom clipboard, utf8str, targets;
		Atoms(Display* dpy) :
			clipboard(XInternAtom(dpy, "CLIPBOARD", false)),
			utf8str(XInternAtom(dpy, "UTF8_STRING", false)),
			targets(XInternAtom(dpy, "TARGETS", false))
		{}
	} atoms;
public:
	XClipboard() :
		dpy(XOpenDisplay(nullptr)),
		root(XDefaultRootWindow(dpy)),
		win(XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0)),
		atoms(dpy)
	{}

	~XClipboard() { XCloseDisplay(dpy); }

	bool waitPaste(std::string clipboard) {
		const Atom targets[2] = { atoms.targets, atoms.utf8str };

		XSetSelectionOwner(dpy, atoms.clipboard, win, CurrentTime);
		bool pasteDone = false;
		XEvent ev;
		while (!pasteDone) {
			XNextEvent(dpy, &ev);

			if (ev.type == SelectionClear) return false;
			if (ev.type != SelectionRequest) continue;

			XSelectionRequestEvent selev = ev.xselectionrequest;

			if (selev.target == atoms.targets)
				XChangeProperty(dpy, selev.requestor, selev.property, XA_ATOM, 32, PropModeReplace, (uint8_t*)targets, 2);
			else {
				XChangeProperty(dpy, selev.requestor, selev.property, selev.target, 8, PropModeReplace, (uint8_t*)clipboard.c_str(), clipboard.length());
				pasteDone = true;
			}

			XSelectionEvent response = {
				.type = SelectionNotify,
				.display = dpy,
				.requestor = selev.requestor,
				.selection = selev.selection,
				.target = selev.target,
				.property = selev.property,
				.time = selev.time
			};
			XSendEvent(dpy, selev.requestor, 0, 0, (XEvent*)&response);
			XFlush(dpy);
		}
		return true;
	}
};
