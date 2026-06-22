#pragma once

#include <helpers/ui/UIScreen.h>
#include "ElementScreen.h"
#include "../MyMesh.h"   // MyMesh::MsgView, OFFLINE_QUEUE_SIZE

class UITask;

// Page order == btn1 navigation order; index used by the bottom page-dots.
enum {
  PAGE_HOME = 0,
  PAGE_MESSAGES,
  PAGE_MESH,
  PAGE_GPS,
  PAGE_BLUETOOTH,
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
  UIElement _items[7];
protected:
  int pageIndex() const override { return PAGE_HOME; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  HomeScreen(UITask* task, NodePrefs* prefs);
};

class MeshScreen : public ElementScreen {
  UIElement _items[9];
protected:
  int pageIndex() const override { return PAGE_MESH; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  MeshScreen(UITask* task, NodePrefs* prefs);
};

class GPSScreen : public ElementScreen {
  UIElement _items[6];
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

#ifndef MSG_PAGE_MAX
  #define MSG_PAGE_MAX 32   // newest-N listed on-device; the queue itself holds more
#endif

// Live view of the offline (unread-by-app) queue: rebuilds from MyMesh every
// render, so it mirrors the queue automatically (messages vanish when the app
// syncs them). Activating a row opens the full-message read view; there is no
// manual dismiss. The per-row strings are a render cache, not a second store.
class MessagesScreen : public ElementScreen {
public:
  struct MsgRef { MessagesScreen* scr; int idx; };   // bound to each row via ctx
private:
  struct Row { char origin[40]; char preview[44]; };
  UIElement _items[MSG_PAGE_MAX + 1];   // +1 for empty-state / overflow label
  MsgRef    _refs[MSG_PAGE_MAX];
  Row       _rows[MSG_PAGE_MAX];
  char      _more[24];                  // "+N more on app" overflow label
protected:
  void rebuild() override;
  int pageIndex() const override { return PAGE_MESSAGES; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  MessagesScreen(UITask* task, NodePrefs* prefs);
  const char* previewAt(int i) const { return _rows[i].preview; }
  void openDetail(int display_idx);     // activate a row -> read view
};

// Full-screen word-wrapped read view for a single message (not an ElementScreen).
class MessageDetailScreen : public UIScreen {
  UITask*         _task;
  MyMesh::MsgView _msg;
  int             _scroll_line;   // first body line shown (overflow paging)
  int             _total_lines;   // set during render
public:
  MessageDetailScreen(UITask* task) : _task(task), _scroll_line(0), _total_lines(0) {
    _msg.body[0] = 0; _msg.sender[0] = 0;
  }
  void setMessage(const MyMesh::MsgView& m) { _msg = m; _scroll_line = 0; }
  void scrollDown();                       // page down, wrapping to top past the end
  int  render(DisplayDriver& display) override;
};

class ShutdownScreen : public ElementScreen {
  UIElement _items[4];
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
