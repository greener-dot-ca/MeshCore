#pragma once

#include <helpers/ui/UIScreen.h>
#include "ElementScreen.h"

class UITask;

// Page order == btn1 navigation order; index used by the bottom page-dots.
enum {
  PAGE_HOME = 0,
  PAGE_MESH,
  PAGE_GPS,
  PAGE_BLUETOOTH,
  PAGE_MESSAGES,
  PAGE_SHUTDOWN,
  PAGE_COUNT
};

// Boot splash (not element-based; mirrors ui-new SplashScreen).
class SplashScreen : public UIScreen {
  UITask* _task;
  unsigned long dismiss_after;
  char _version_info[12];
public:
  SplashScreen(UITask* task);
  int render(DisplayDriver& display) override;
  void poll() override;
};

class HomeScreen : public ElementScreen {
  UIElement _items[8];
protected:
  int pageIndex() const override { return PAGE_HOME; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  HomeScreen(UITask* task, NodePrefs* prefs);
};

class MeshScreen : public ElementScreen {
  UIElement _items[8];
protected:
  int pageIndex() const override { return PAGE_MESH; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  MeshScreen(UITask* task, NodePrefs* prefs);
};

class GPSScreen : public ElementScreen {
  UIElement _items[5];
protected:
  int pageIndex() const override { return PAGE_GPS; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  GPSScreen(UITask* task, NodePrefs* prefs);
};

class BluetoothScreen : public ElementScreen {
  UIElement _items[3];
protected:
  int pageIndex() const override { return PAGE_BLUETOOTH; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  BluetoothScreen(UITask* task, NodePrefs* prefs);
};

#ifndef MAX_UNREAD_MSGS
  #define MAX_UNREAD_MSGS 16
#endif

class MessagesScreen : public ElementScreen {
public:
  struct MsgEntry {
    uint32_t timestamp;
    char origin[62];
    char msg[78];
  };
  struct MsgRef { MessagesScreen* scr; MsgEntry* e; };  // bound to each message element via ctx
private:
  UIElement _items[MAX_UNREAD_MSGS + 1];   // +1 for the empty-state label
  MsgRef    _refs[MAX_UNREAD_MSGS];
  MsgEntry  _msgs[MAX_UNREAD_MSGS];         // chronological, newest at _msgs[_num-1]
  int       _num;
protected:
  void rebuild() override;
  int pageIndex() const override { return PAGE_MESSAGES; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  MessagesScreen(UITask* task, NodePrefs* prefs);
  void addPreview(uint8_t path_len, const char* from_name, const char* msg);
  int  numUnread() const { return _num; }
  void removeEntry(MsgEntry* e);   // dismiss one preview
};

class ShutdownScreen : public ElementScreen {
  UIElement _items[3];
  bool _shutdown_init;
protected:
  void rebuild() override;
  int pageIndex() const override { return PAGE_SHUTDOWN; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  ShutdownScreen(UITask* task, NodePrefs* prefs);
  void poll() override;
  void initShutdown() { _shutdown_init = true; }
};
