#pragma once

#include <functional>
#include <libnotify/notification.h>
#include <string>
#include <libnotify/notify.h>

#include <glib.h>

class Notifications {
public:
	class Notification {
		friend class Notifications;
		NotifyNotification* notification;
		Notification(NotifyNotification* notification) : notification(notification) {}

		using callbackFunction = std::function<void(Notification, const std::string& actionId, void* userData)>;
		struct callbackData {
			callbackFunction function;
			void* userData;
		};
		static void callbackWrapper(NotifyNotification* inNotification, const char* inAction, void* inData) {
			callbackData* data = (callbackData*)inData;	
			data->function(Notification(inNotification), inAction, data->userData);
		}
		static void callbackFree(void* inData) {
			delete (callbackData*)inData;
		}
	public:
		Notification& setUrgency(NotifyUrgency urgency) {
			notify_notification_set_urgency(notification, urgency);
			return *this;
			GFreeFunc f;
		}

		Notification& timeout(int timeout) {
			notify_notification_set_timeout(notification, timeout);
			return *this;
		}

		Notification& addAction(const std::string& id, const std::string& label, callbackFunction action, void* userData = nullptr) {
			callbackData* data = new callbackData{action, userData};
			notify_notification_add_action(notification, id.c_str(), label.c_str(), (NotifyActionCallback)callbackWrapper, data, callbackFree);
			return *this;
		}

		Notification& show() {
			notify_notification_show(notification, nullptr); // TODO: check errors
			return *this;

		}
		void clear() {
			notify_notification_close(notification, nullptr); // TODO: check errors
		}
	};

	Notifications(const std::string& name) {
		notify_init(name.c_str()); // TODO: check return code
	}

	Notification create(const std::string& title, const std::string& content) {
		Notification notification(notify_notification_new(title.c_str(), content.c_str(), nullptr));
		return notification;
	}

};
