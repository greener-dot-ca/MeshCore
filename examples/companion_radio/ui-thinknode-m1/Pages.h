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
  PAGE_RXLOG,
  PAGE_RADIO,
  PAGE_GPS,
  PAGE_BLUETOOTH,
  PAGE_BUZZ,
  PAGE_TIME,
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
  UIElement _items[6];
protected:
  int pageIndex() const override { return PAGE_MESH; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  MeshScreen(UITask* task, NodePrefs* prefs);
};

// Radio page: RF config + reception. The "Off-grid" client-repeat toggle and the
// 433/869/918 MHz preset selector live here (moved off the Mesh page), alongside
// the current LoRa parameters (freq/BW/SF/CR/TX, read-only -- set via the app) and
// the live link readings (noise floor, last-packet RSSI/SNR).
class RadioScreen : public ElementScreen {
  UIElement _items[10];
protected:
  int pageIndex() const override { return PAGE_RADIO; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  RadioScreen(UITask* task, NodePrefs* prefs);
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
  UIElement _items[4];
protected:
  int pageIndex() const override { return PAGE_BLUETOOTH; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  BluetoothScreen(UITask* task, NodePrefs* prefs);
};

// Buzzer settings. For now just the on/off toggle; planned: customizable buzz
// patterns (e.g. morse-beep the hop count on RX).
class BuzzScreen : public ElementScreen {
  UIElement _items[2];
protected:
  int pageIndex() const override { return PAGE_BUZZ; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  BuzzScreen(UITask* task, NodePrefs* prefs);
};

// Displayed-time settings: 12/24h format and UTC offset (controls the status-bar
// clock and the message read-view timestamp).
class TimeScreen : public ElementScreen {
  UIElement _items[5];
protected:
  int pageIndex() const override { return PAGE_TIME; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  TimeScreen(UITask* task, NodePrefs* prefs);
};

#ifndef MSG_PAGE_MAX
  #define MSG_PAGE_MAX 32   // newest-N listed on-device; the queue itself holds more
#endif

// Live, drill-down view over the offline (unread-by-app) queue, rebuilt from
// MyMesh every render so it mirrors the queue (messages vanish when the app
// syncs them). Two levels:
//   L_CONV  - conversations: each channel + each DM contact, with msg count + last
//   L_MSGS  - every message in the selected conversation (newest first; channel
//             rows are node-prefixed "Node: body"); activate -> read view
// The per-row strings are a render cache, not a second store.
class MessagesScreen : public ElementScreen {
public:
  struct MsgRef { MessagesScreen* scr; int idx; };   // bound to each row via ctx
  enum Level : uint8_t { L_CONV, L_MSGS };
private:
  struct Row { char line[80]; char time[8]; };  // row text + relative time
  UIElement _items[MSG_PAGE_MAX + 1];   // +1 for empty-state / overflow label
  MsgRef    _refs[MSG_PAGE_MAX];
  Row       _rows[MSG_PAGE_MAX];
  char      _more[24];                  // "+N more on app" overflow label

  Level _level = L_CONV;
  bool  _sel_is_channel = false;        // type of the selected conversation
  char  _sel_conv[32] = {0};            // selected channel or contact name
  // At L_CONV, _refs[i].idx indexes these per-row keys; at L_MSGS, _refs[i].idx
  // is the absolute display index for getDisplayMsg/openDetail.
  char  _key_name[MSG_PAGE_MAX][32];
  bool  _key_chan[MSG_PAGE_MAX];

  void rebuildConversations();
  void rebuildMessages();
protected:
  void rebuild() override;
  int pageIndex() const override { return PAGE_MESSAGES; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  MessagesScreen(UITask* task, NodePrefs* prefs);
  const char* timeAt(int i) const { return _rows[i].time; }   // right-aligned relative time
  void openDetail(int display_idx);     // activate a row -> read view
  void selectConversation(int row);     // L_CONV row activate -> messages
  bool atTopLevel() const { return _level == L_CONV; }
  void goBack();                        // pop up to the conversation list
  void resetFocus() override;           // re-entering the page starts at L_CONV
};

// Full-screen word-wrapped read view for a single message (not an ElementScreen).
// Holds its position in the message list (_idx/_total) so triangle can page
// through both the body and then on to the next message, with a list scrollbar.
class MessageDetailScreen : public UIScreen {
  UITask*         _task;
  MyMesh::MsgView _msg;
  int             _scroll_line;   // first body line shown (overflow paging)
  int             _total_lines;   // set during render
  int             _idx;           // this message's index in the list (0 = newest)
  int             _total;         // total messages in the list
public:
  MessageDetailScreen(UITask* task)
    : _task(task), _scroll_line(0), _total_lines(0), _idx(0), _total(0) {
    _msg.body[0] = 0; _msg.sender[0] = 0;
  }
  void setMessage(const MyMesh::MsgView& m, int idx, int total) {
    _msg = m; _scroll_line = 0; _idx = idx; _total = total;
  }
  int  index() const { return _idx; }
  bool scrollDown();                       // page down; false when already at the end
  int  render(DisplayDriver& display) override;
};

// Live RX log: every packet the radio decodes, newest first, each row showing the
// packet type, signal (RSSI/SNR), hop count and -- when the decode resolves one -- the
// sender/channel name. Rebuilt every render from MyMesh's rx_log ring (the source of
// truth); rows scroll but don't drill down. Clock time ("14:02") is right-aligned.
class RxLogScreen : public ElementScreen {
public:
  struct RxRef { RxLogScreen* scr; int idx; };
private:
  struct Row { char line[40]; char time[8]; };   // "TYPE -rssi/+snr name" + HH:MM
  UIElement  _items[RX_LOG_SIZE + 1];             // +1 for the empty-state label
  RxRef      _refs[RX_LOG_SIZE];
  Row        _rows[RX_LOG_SIZE];
protected:
  void rebuild() override;
  int pageIndex() const override { return PAGE_RXLOG; }
  int pageCount() const override { return PAGE_COUNT; }
public:
  RxLogScreen(UITask* task, NodePrefs* prefs);
  const char* timeAt(int i) const { return _rows[i].time; }   // right-aligned HH:MM
};

// Pop-up help overlay (not a carousel page -- shown like the message read view):
// explains the status-bar icons and the two-button navigation. Any press dismisses.
class HelpScreen : public UIScreen {
public:
  HelpScreen() {}
  int render(DisplayDriver& display) override;
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
