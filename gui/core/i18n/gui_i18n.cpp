#include "gui/core/i18n/gui_i18n.h"

#include <cstring>

GuiLanguage gui_language_from_locale_name(const QString &locale_name) {
  const QString norm = locale_name.trimmed().toLower();
  if (norm.startsWith("zh-tw") || norm.startsWith("zh-hant") ||
      norm.startsWith("zh-hk") || norm.startsWith("zh-mo")) {
    return GuiLanguage::ZhTw;
  }
  return GuiLanguage::English;
}

bool gui_language_is_zh_tw(GuiLanguage language) {
  return language == GuiLanguage::ZhTw;
}

QString gui_i18n_text(GuiLanguage language, const char *key) {
  const bool zh = gui_language_is_zh_tw(language);

  if (std::strcmp(key, "search.placeholder") == 0) {
    return zh ? QString::fromUtf8("地點關鍵字/地址/經緯度搜尋")
              : QString("Search a location and press Enter");
  }
  if (std::strcmp(key, "search.no_match") == 0) {
    return zh ? QString::fromUtf8("找不到符合搜尋條件的地點。")
              : QString("No place matched your search text.");
  }
  if (std::strcmp(key, "alert.spoof_rinex_required") == 0) {
    return zh ? QString::fromUtf8("SPOOF/CROSSBOW 模式需要有效的 RINEX 檔案，目前僅可使用 JAM。")
              : QString("SPOOF/CROSSBOW mode requires a valid RINEX file. JAM only is available right now.");
  }

  if (std::strcmp(key, "rid_rx.active") == 0) {
    return zh ? QString::fromUtf8("[RID] Remote ID 接收已啟動，監聽 BT LE @ 2.426 GHz...")
              : QString("[RID] Remote ID receiver active, scanning BT LE @ 2.426 GHz...");
  }
  if (std::strcmp(key, "gnss_rx.searching") == 0) {
    return zh ? QString::fromUtf8("[GPS] USRP 接收衛星中，自動計算初始座標...")
              : QString("[GPS] USRP receiving satellites, computing initial position...");
  }
  if (std::strcmp(key, "gnss_rx.timeout") == 0) {
    return zh ? QString::fromUtf8("[GPS] 衛星定位逾時，請手動選擇起始座標")
              : QString("[GPS] Satellite fix timed out, please select a start position manually");
  }

  if (std::strcmp(key, "path.road_ready") == 0) {
    return zh ? QString::fromUtf8("道路預覽已就緒：按右鍵確認")
              : QString("Road preview ready: right-click to confirm");
  }
  if (std::strcmp(key, "path.road_failed") == 0) {
    return zh ? QString::fromUtf8("道路規劃失敗：找不到可行駛路線")
              : QString("Road routing failed: no drivable map road path");
  }
  if (std::strcmp(key, "path.need_anchor") == 0) {
    return zh ? QString::fromUtf8("路徑規劃需要有效的起始錨點")
              : QString("Path planning needs a valid start anchor");
  }
  if (std::strcmp(key, "path.too_close") == 0) {
    return zh ? QString::fromUtf8("預覽點距離起點太近")
              : QString("Preview point too close to start");
  }
  if (std::strcmp(key, "path.line_ready") == 0) {
    return zh ? QString::fromUtf8("直線預覽已就緒：按右鍵確認")
              : QString("Line preview ready: right-click to confirm");
  }
  if (std::strcmp(key, "path.road_ready_cached") == 0) {
    return zh ? QString::fromUtf8("道路預覽已就緒（快取）：按右鍵確認")
              : QString("Road preview ready (cached): right-click to confirm");
  }
  if (std::strcmp(key, "path.road_loading") == 0) {
    return zh ? QString::fromUtf8("道路預覽載入中...")
              : QString("Road preview loading...");
  }
  if (std::strcmp(key, "path.no_preview") == 0) {
    return zh ? QString::fromUtf8("沒有可確認的預覽路徑")
              : QString("No preview path to confirm");
  }
  if (std::strcmp(key, "path.queue_full") == 0) {
    return zh ? QString::fromUtf8("路徑佇列已滿（最多 5 段）")
              : QString("Path queue is full (max 5)");
  }
  if (std::strcmp(key, "path.build_failed") == 0) {
    return zh ? QString::fromUtf8("無法從預覽建立路徑檔")
              : QString("Failed to build path file from preview");
  }
  if (std::strcmp(key, "path.queue_reject") == 0) {
    return zh ? QString::fromUtf8("佇列拒絕加入路徑（最多 5 段）")
              : QString("Queue rejected path (max 5 active segments)");
  }
  if (std::strcmp(key, "path.segment_queued") == 0) {
    return zh ? QString::fromUtf8("路徑段已確認並加入佇列")
              : QString("Segment confirmed and queued");
  }
  if (std::strcmp(key, "path.undo_fail") == 0) {
    return zh ? QString::fromUtf8("無法刪除路徑：最後一段已執行或佇列為空")
              : QString("Cannot undo: last segment is already executing or queue is empty");
  }
  if (std::strcmp(key, "path.undo_ok") == 0) {
    return zh ? QString::fromUtf8("已刪除最後一段佇列路徑")
              : QString("Last queued segment has been undone");
  }

  if (std::strcmp(key, "panel.signal_settings") == 0) {
    return zh ? QString::fromUtf8("訊號設定") : QString("Signal Settings");
  }
  if (std::strcmp(key, "panel.sats_none") == 0) {
    return zh ? QString::fromUtf8("衛星 | 無") : QString("SATS | none");
  }
  if (std::strcmp(key, "panel.sats_prefix") == 0) {
    return zh ? QString::fromUtf8("衛星 |") : QString("SATS |");
  }
  if (std::strcmp(key, "panel.sats_gps_prefix") == 0) {
    return zh ? QString::fromUtf8("GPS |") : QString("GPS |");
  }
  if (std::strcmp(key, "panel.sats_bds_prefix") == 0) {
    return zh ? QString::fromUtf8("BDS |") : QString("BDS |");
  }
  if (std::strcmp(key, "panel.sats_more") == 0) {
    return zh ? QString::fromUtf8(" ...（共 %1 顆）") : QString(" ... (%1)");
  }

  if (std::strcmp(key, "tab.simple") == 0) {
    return zh ? QString::fromUtf8("基礎") : QString("SIMPLE");
  }
  if (std::strcmp(key, "tab.detail") == 0) {
    return zh ? QString::fromUtf8("進階") : QString("DETAIL");
  }
  if (std::strcmp(key, "label.interfere") == 0) {
    return zh ? QString::fromUtf8("攻擊模式") : QString("INTERFERE");
  }
  if (std::strcmp(key, "label.spoof") == 0) {
    return zh ? QString::fromUtf8("欺騙") : QString("SPOOF");
  }
  if (std::strcmp(key, "label.crossbow") == 0) {
    return zh ? QString::fromUtf8("定位彈弓") : QString("CROSSBOW");
  }
  if (std::strcmp(key, "label.jam") == 0) {
    return zh ? QString::fromUtf8("干擾") : QString("JAM");
  }
  if (std::strcmp(key, "label.system") == 0) {
    return zh ? QString::fromUtf8("系統選擇") : QString("SYSTEM");
  }
  if (std::strcmp(key, "label.fs") == 0) {
    return zh ? QString::fromUtf8("取樣率") : QString("FS (Frequency)");
  }
  if (std::strcmp(key, "label.signal_gain") == 0) {
    return zh ? QString::fromUtf8("訊號增益") : QString("Signal Gain");
  }
  if (std::strcmp(key, "label.tx_gain") == 0) {
    return zh ? QString::fromUtf8("發射增益") : QString("TX (Transmit Gain)");
  }
  if (std::strcmp(key, "label.target_cn0") == 0) {
    return zh ? QString::fromUtf8("目標 C/N0") : QString("Target C/N0");
  }
  if (std::strcmp(key, "label.prn_select") == 0) {
    return zh ? QString::fromUtf8("PRN 選擇") : QString("PRN Select");
  }
  if (std::strcmp(key, "label.path_vmax") == 0) {
    return zh ? QString::fromUtf8("路徑時速上限") : QString("Path Vmax");
  }
  if (std::strcmp(key, "label.path_acc") == 0) {
    return zh ? QString::fromUtf8("路徑加速度") : QString("Path Acc");
  }
  if (std::strcmp(key, "label.iono") == 0) {
    return zh ? QString::fromUtf8("電離層") : QString("IONO");
  }
  if (std::strcmp(key, "label.ext_clk") == 0) {
    return zh ? QString::fromUtf8("外部時鐘") : QString("EXT CLK");
  }
  if (std::strcmp(key, "label.max_ch") == 0) {
    return zh ? QString::fromUtf8("最大通道數") : QString("Max CH");
  }
  if (std::strcmp(key, "label.mode") == 0) {
    return zh ? QString::fromUtf8("衛星範圍") : QString("MODE");
  }
  if (std::strcmp(key, "label.format") == 0) {
    return zh ? QString::fromUtf8("訊號格式") : QString("FORMAT");
  }
  if (std::strcmp(key, "label.start") == 0) {
    return zh ? QString::fromUtf8("開始執行") : QString("START");
  }
  if (std::strcmp(key, "label.exit") == 0) {
    return zh ? QString::fromUtf8("中止系統") : QString("EXIT");
  }
  if (std::strcmp(key, "value.prn") == 0) {
    return QString("PRN %1");
  }

  if (std::strcmp(key, "osm.back") == 0) {
    return zh ? QString::fromUtf8("路徑刪除") : QString("BACK");
  }
  if (std::strcmp(key, "osm.nfz_on") == 0) {
    return zh ? QString::fromUtf8("禁飛區") : QString("NFZ ON");
  }
  if (std::strcmp(key, "osm.nfz_off") == 0) {
    return zh ? QString::fromUtf8("禁飛區") : QString("NFZ OFF");
  }
  if (std::strcmp(key, "osm.light") == 0) {
     return zh ? QString::fromUtf8("衛星") : QString("SATELLITE");
  }
  if (std::strcmp(key, "osm.dark") == 0) {
     return zh ? QString::fromUtf8("街道") : QString("STREET");
  }
  if (std::strcmp(key, "osm.return") == 0) {
    return zh ? QString::fromUtf8("地圖復原") : QString("RETURN");
  }
  if (std::strcmp(key, "osm.stop") == 0) {
    return zh ? QString::fromUtf8("停止執行") : QString("STOP");
  }
  if (std::strcmp(key, "osm.run_time") == 0) {
    return zh ? QString::fromUtf8("執行時間") : QString("RUN TIME");
  }
  if (std::strcmp(key, "osm.init_time") == 0) {
    return zh ? QString::fromUtf8("初始化中") : QString("INITIALIZING");
  }
  if (std::strcmp(key, "osm.target_distance_fmt") == 0) {
    return zh ? QString::fromUtf8("距離目標 %1 公里")
              : QString("TO TARGET %1 km");
  }
  if (std::strcmp(key, "osm.remaining_distance_fmt") == 0) {
    return zh ? QString::fromUtf8("剩餘距離 %1 公里")
              : QString("REMAIN %1 km");
  }
  if (std::strcmp(key, "osm.legend_restricted_core") == 0) {
    return zh ? QString::fromUtf8("禁飛區") : QString("Core Restricted (Red)");
  }
  if (std::strcmp(key, "osm.lang_btn") == 0) {
    return zh ? QString::fromUtf8("中文") : QString("English");
  }

  if (std::strcmp(key, "map.sat_legend.receiver") == 0) {
    return zh ? QString::fromUtf8("接收機") : QString("Receiver");
  }
  if (std::strcmp(key, "map.sat_legend.visible") == 0) {
    return zh ? QString::fromUtf8("一般可見") : QString("Visible");
  }
  if (std::strcmp(key, "map.sat_legend.standby") == 0) {
    return zh ? QString::fromUtf8("選中待命") : QString("Standby");
  }
  if (std::strcmp(key, "map.sat_legend.running") == 0) {
    return zh ? QString::fromUtf8("執行中") : QString("Running");
  }
  if (std::strcmp(key, "map.sat_legend.gps") == 0) {
    return QString("GPS");
  }
  if (std::strcmp(key, "map.sat_legend.bds") == 0) {
    return QString("BDS");
  }

  if (std::strcmp(key, "status.current") == 0) {
    return zh ? QString::fromUtf8("當下座標 %1, %2")
              : QString("Receiver %1, %2");
  }
  if (std::strcmp(key, "status.current_na") == 0) {
    return zh ? QString::fromUtf8("當下座標 N/A")
              : QString("Receiver N/A");
  }
  if (std::strcmp(key, "monitor.spectrum") == 0) {
    return zh ? QString::fromUtf8("訊號頻譜") : QString("Signal Spectrum");
  }
  if (std::strcmp(key, "monitor.waterfall") == 0) {
    return zh ? QString::fromUtf8("訊號瀑布圖") : QString("Signal Waterfall");
  }
  if (std::strcmp(key, "monitor.time") == 0) {
    return zh ? QString::fromUtf8("訊號時域") : QString("Signal Time-Domain");
  }
  if (std::strcmp(key, "monitor.constellation") == 0) {
    return zh ? QString::fromUtf8("訊號星座圖") : QString("Signal Constellation");
  }

  if (std::strcmp(key, "style.dialog.title") == 0) {
    return zh ? QString::fromUtf8("Signal Setting 樣貌設定")
              : QString("Signal Setting Appearance");
  }
  if (std::strcmp(key, "style.live_preview") == 0) {
    return zh ? QString::fromUtf8("即時預覽") : QString("Live Preview");
  }
  if (std::strcmp(key, "style.accent") == 0) {
    return zh ? QString::fromUtf8("主色 (滑桿/高亮)")
              : QString("Accent (Slider/Highlight)");
  }
  if (std::strcmp(key, "style.border") == 0) {
    return zh ? QString::fromUtf8("邊框 (外框/分隔線)")
              : QString("Border (Frames/Dividers)");
  }
  if (std::strcmp(key, "style.text_primary") == 0) {
    return zh ? QString::fromUtf8("主要文字 (標題/內容)")
              : QString("Text (Primary)");
  }
  if (std::strcmp(key, "style.text_dim") == 0) {
    return zh ? QString::fromUtf8("次要文字 (停用/註解)")
              : QString("Dim Text (Disabled/Secondary)");
  }
  if (std::strcmp(key, "style.custom_color") == 0) {
    return zh ? QString::fromUtf8("Custom 顏色...") : QString("Custom Color...");
  }
  if (std::strcmp(key, "style.pick_accent") == 0) {
    return zh ? QString::fromUtf8("選擇主題色") : QString("Select Accent Color");
  }
  if (std::strcmp(key, "style.pick_border") == 0) {
    return zh ? QString::fromUtf8("選擇邊框顏色") : QString("Select Border Color");
  }
  if (std::strcmp(key, "style.pick_text_primary") == 0) {
    return zh ? QString::fromUtf8("選擇主要文字顏色")
              : QString("Select Text Color");
  }
  if (std::strcmp(key, "style.pick_text_dim") == 0) {
    return zh ? QString::fromUtf8("選擇次要文字顏色")
              : QString("Select Dim Text Color");
  }
  if (std::strcmp(key, "style.pick_custom") == 0) {
    return zh ? QString::fromUtf8("選擇自訂顏色")
              : QString("Select Custom Color");
  }
  if (std::strcmp(key, "style.reset_defaults") == 0) {
    return zh ? QString::fromUtf8("恢復預設") : QString("Reset Defaults");
  }
  if (std::strcmp(key, "style.row.master_text") == 0) {
    return zh ? QString::fromUtf8("整體文字") : QString("Master Text");
  }
  if (std::strcmp(key, "style.row.caption_text") == 0) {
    return zh ? QString::fromUtf8("Caption 文字") : QString("Caption Text");
  }
  if (std::strcmp(key, "style.row.switch_option_text") == 0) {
    return zh ? QString::fromUtf8("Switch 選項文字")
              : QString("Switch Option Text");
  }
  if (std::strcmp(key, "style.row.value_text") == 0) {
    return zh ? QString::fromUtf8("輸入框數值文字") : QString("Value Text");
  }
  if (std::strcmp(key, "style.row.colors") == 0) {
    return zh ? QString::fromUtf8("顏色") : QString("Colors");
  }
  if (std::strcmp(key, "style.row.optional_fonts") == 0) {
    return zh ? QString::fromUtf8("選用字型") : QString("Optional Fonts");
  }
  if (std::strcmp(key, "style.font.times_bold") == 0) {
    return zh ? QString::fromUtf8("Times New Roman 粗體 (timesbd.ttf)")
              : QString("Times New Roman Bold (timesbd.ttf)");
  }
  if (std::strcmp(key, "style.font.times_italic") == 0) {
    return zh ? QString::fromUtf8("Times New Roman 斜體 (timesi.ttf)")
              : QString("Times New Roman Italic (timesi.ttf)");
  }
  if (std::strcmp(key, "style.font.times_bold_italic") == 0) {
    return zh ? QString::fromUtf8("Times New Roman 粗斜體 (timesbi.ttf)")
              : QString("Times New Roman Bold Italic (timesbi.ttf)");
  }
  if (std::strcmp(key, "style.font.zh_kai") == 0) {
    return zh ? QString::fromUtf8("中文翻譯使用標楷體 (kaiu.ttf)")
              : QString("Use BiauKai for Chinese Translations (kaiu.ttf)");
  }

  if (std::strcmp(key, "tutorial.flow.title.0") == 0) {
    return zh ? QString::fromUtf8("導覽總覽") : QString("Guide Overview");
  }
  if (std::strcmp(key, "tutorial.flow.title.1") == 0) {
    return zh ? QString::fromUtf8("地圖操作與圖層") : QString("Map Controls & Layers");
  }
  if (std::strcmp(key, "tutorial.flow.title.2") == 0) {
    return zh ? QString::fromUtf8("狀態列與任務資訊") : QString("Status & Mission Info");
  }
  if (std::strcmp(key, "tutorial.flow.title.3") == 0) {
    return zh ? QString::fromUtf8("星下點") : QString("Skyplot");
  }
  if (std::strcmp(key, "tutorial.flow.title.4") == 0) {
    return zh ? QString::fromUtf8("四波形") : QString("Four Waveforms");
  }
  if (std::strcmp(key, "tutorial.flow.title.5") == 0) {
    return QString("Simple Page Buttons (1/2)");
  }
  if (std::strcmp(key, "tutorial.flow.title.6") == 0) {
    return QString("Simple Page Buttons (2/2)");
  }
  if (std::strcmp(key, "tutorial.flow.title.7") == 0) {
    return QString("Detail Page Buttons (1/2)");
  }
  if (std::strcmp(key, "tutorial.flow.title.8") == 0) {
    return QString("Detail Page Buttons (2/2)");
  }

  if (std::strcmp(key, "tutorial.flow.body.0") == 0) {
    return zh
               ? QString::fromUtf8("這份導覽聚焦在地圖任務流程。\n你可以先從目錄直接跳章，或用 NEXT 依序完成一輪。")
               : QString("This guide focuses on map mission workflow.\nJump by section from the overview, or use NEXT for a full pass.");
  }
  if (std::strcmp(key, "tutorial.flow.body.1") == 0) {
    return zh ? QString::fromUtf8("本節介紹地圖上方的核心控制：搜尋、NFZ、明暗模式、語言與 GUIDE。\n先熟悉這一排，後續操作會快很多。")
              : QString("This section covers the top control row: Search, NFZ, theme, language, and GUIDE.\nMaster these first to speed up every mission.");
  }
  if (std::strcmp(key, "tutorial.flow.body.2") == 0) {
    return zh ? QString::fromUtf8("本節介紹右下狀態徽章與任務資訊列：倍率、提示、路徑狀態與座標。\n排錯與確認任務是否生效，主要都看這裡。")
              : QString("This section explains the bottom-right badges: scale, tips, path status, and coordinates.\nUse this area to verify mission state and troubleshoot quickly.");
  }
  if (std::strcmp(key, "tutorial.flow.body.3") == 0) {
    return zh ? QString::fromUtf8("此環節說明星下點顯示，包含 G 與 C 衛星。")
              : QString("This part explains skyplot with G and C satellites.");
  }
  if (std::strcmp(key, "tutorial.flow.body.4") == 0) {
    return zh ? QString::fromUtf8("此環節說明四個波形面板的觀察重點。")
              : QString("This part explains the four waveform panels.");
  }
  if (std::strcmp(key, "tutorial.flow.body.5") == 0) {
    return zh ? QString::fromUtf8("此環節說明 Simple 頁面上半部與頁籤的 6 個重點按鈕。")
              : QString("This part explains 6 key controls in the upper and tab areas of the Simple page.");
  }
  if (std::strcmp(key, "tutorial.flow.body.6") == 0) {
    return zh ? QString::fromUtf8("此環節說明 Simple 頁面下半部的 5 個重點控制。")
              : QString("This part explains 5 key controls in the lower area of the Simple page.");
  }
  if (std::strcmp(key, "tutorial.flow.body.7") == 0) {
    return zh ? QString::fromUtf8("此環節說明 Detail 頁面第一部分的重點按鈕。")
              : QString("This part explains the first half of key controls on the Detail page.");
  }
  if (std::strcmp(key, "tutorial.flow.body.8") == 0) {
    return zh ? QString::fromUtf8("此環節說明 Detail 頁面第二部分的重點按鈕。")
              : QString("This part explains the second half of key controls on the Detail page.");
  }

  if (std::strcmp(key, "tutorial.btn.prev") == 0) {
    return zh ? QString::fromUtf8("上一步") : QString("PREV");
  }
  if (std::strcmp(key, "tutorial.btn.next") == 0) {
    return zh ? QString::fromUtf8("下一步") : QString("NEXT");
  }
  if (std::strcmp(key, "tutorial.btn.done") == 0) {
    return zh ? QString::fromUtf8("完成") : QString("DONE");
  }
  if (std::strcmp(key, "tutorial.btn.exit") == 0) {
    return zh ? QString::fromUtf8("離開") : QString("EXIT");
  }
  if (std::strcmp(key, "tutorial.btn.contents") == 0) {
    return zh ? QString::fromUtf8("目錄") : QString("CONTENTS");
  }
  if (std::strcmp(key, "tutorial.overlay.toc_header") == 0) {
    return zh ? QString::fromUtf8("任務導覽  ❖  章節選單")
              : QString("MISSION GUIDE  ❖  CONTENTS");
  }
  if (std::strcmp(key, "tutorial.overlay.part_title") == 0) {
    return zh ? QString::fromUtf8("環節 %1/%2  %3") : QString("Part %1/%2  %3");
  }
  if (std::strcmp(key, "tutorial.toc.section.map") == 0) {
    return zh ? QString::fromUtf8("地圖") : QString("Map");
  }
  if (std::strcmp(key, "tutorial.toc.section.skyplot") == 0) {
    return zh ? QString::fromUtf8("星下點") : QString("Skyplot");
  }
  if (std::strcmp(key, "tutorial.toc.section.waveforms") == 0) {
    return zh ? QString::fromUtf8("四波形") : QString("Waveforms");
  }
  if (std::strcmp(key, "tutorial.toc.section.simple") == 0) {
    return zh ? QString::fromUtf8("基礎頁面設定") : QString("Basic Page Settings");
  }
  if (std::strcmp(key, "tutorial.toc.section.detail") == 0) {
    return zh ? QString::fromUtf8("進階頁面設定") : QString("Advanced Page Settings");
  }
  if (std::strcmp(key, "tutorial.toc.range.map") == 0) {
    return zh ? QString::fromUtf8("環節 1-2（地圖）") : QString("Parts 1-2 (Map)");
  }
  if (std::strcmp(key, "tutorial.toc.range.skyplot") == 0) {
    return zh ? QString::fromUtf8("環節 3") : QString("Part 3");
  }
  if (std::strcmp(key, "tutorial.toc.range.waveforms") == 0) {
    return zh ? QString::fromUtf8("環節 4") : QString("Part 4");
  }
  if (std::strcmp(key, "tutorial.toc.range.simple") == 0) {
    return zh ? QString::fromUtf8("環節 5-6") : QString("Parts 5-6");
  }
  if (std::strcmp(key, "tutorial.toc.range.detail") == 0) {
    return zh ? QString::fromUtf8("環節 7-8") : QString("Parts 7-8");
  }

  if (std::strcmp(key, "tutorial.callout.default.map_top_btn") == 0) {
    return zh ? QString::fromUtf8("按鈕功能說明\nabcdefghijklmn\nopqrstuvwxyz")
              : QString("Button Function Summary\nOverview of top control buttons and their usage.");
  }
  if (std::strcmp(key, "tutorial.callout.default.map_top_info") == 0) {
    return zh ? QString::fromUtf8("資訊行與狀態區\nabcdefghijklmn\nopqrstuvwxyz")
              : QString("Info and Status Row\nDisplays key runtime status and map-related indicators.");
  }
  if (std::strcmp(key, "tutorial.callout.default.map_bottom_left") == 0) {
    return zh ? QString::fromUtf8("左下區塊說明\nabcdefghijklmn\nopqrstuvwxyz")
              : QString("Bottom-Left Area\nExplanation of controls and information shown in this region.");
  }
  if (std::strcmp(key, "tutorial.callout.default.map_bottom_right") == 0) {
    return zh ? QString::fromUtf8("右下區塊說明\nabcdefghijklmn\nopqrstuvwxyz")
              : QString("Bottom-Right Area\nExplanation of controls and information shown in this region.");
  }
  if (std::strcmp(key, "tutorial.callout.default.ssp_g") == 0) {
    return zh ? QString::fromUtf8("GPS 衛星群組\nabcdefghijklmn\nopqrstuvwxyz")
              : QString("GPS Satellite Group\nGPS satellite items and their current status.");
  }
  if (std::strcmp(key, "tutorial.callout.default.ssp_c") == 0) {
    return zh ? QString::fromUtf8("BDS 衛星群組\nabcdefghijklmn\nopqrstuvwxyz")
              : QString("BDS Satellite Group\nBDS satellite items and their current status.");
  }
  if (std::strcmp(key, "tutorial.callout.default.sig_simple") == 0) {
    return zh ? QString::fromUtf8("Simple 模式控制區\nabcdefghijklmn\nopqrstuvwxyz")
              : QString("Basic Mode Controls\nCore controls for quick setup and operation.");
  }
  if (std::strcmp(key, "tutorial.callout.default.sig_detail") == 0) {
    return zh ? QString::fromUtf8("Detail 模式控制區\nabcdefghijklmn\nopqrstuvwxyz")
              : QString("Advanced Mode Controls\nExtended controls for detailed configuration.");
  }
  if (std::strcmp(key, "tutorial.callout.default.wave_amp") == 0) {
    return zh ? QString::fromUtf8("振幅區塊\nabcdefghijklmn\nopqrstuvwxyz")
              : QString("Amplitude Section\nShows signal amplitude-related information.");
  }
  if (std::strcmp(key, "tutorial.callout.default.wave_bw") == 0) {
    return zh ? QString::fromUtf8("頻寬區塊\nabcdefghijklmn\nopqrstuvwxyz")
              : QString("Bandwidth Section\nShows spectrum width and frequency span context.");
  }
  if (std::strcmp(key, "tutorial.callout.default.wave_noise") == 0) {
    return zh ? QString::fromUtf8("雜訊區塊\nabcdefghijklmn\nopqrstuvwxyz")
              : QString("Noise Section\nShows noise characteristics and related behavior.");
  }
  if (std::strcmp(key, "tutorial.callout.default.wave_mode") == 0) {
    return zh ? QString::fromUtf8("模式切換區\nabcdefghijklmn\nopqrstuvwxyz")
              : QString("Mode Switch Area\nUsed to switch display or operation mode.");
  }

  if (std::strcmp(key, "tutorial.callout.step1.search_box") == 0) {
    return zh ? QString::fromUtf8("搜尋框\n地點關鍵字/地址/經緯度搜尋。")
              : QString("Search Box\nEnter place name or coordinates (e.g. 22.62,120.30), then press Enter.");
  }
  if (std::strcmp(key, "tutorial.callout.step1.nfz_btn") == 0) {
    return zh ? QString::fromUtf8("禁飛區\nDJI 禁航區範圍。")
              : QString("NFZ Button\nToggle no-fly layers. Disable temporarily if you need a cleaner planning view.");
  }
  if (std::strcmp(key, "tutorial.callout.step1.dark_mode_btn") == 0) {
    return zh ? QString::fromUtf8("街道/衛星\n地圖類型（街道/衛星）。")
              : QString("Theme Toggle\nSwitch map brightness for better readability in day/night environments.");
  }
  if (std::strcmp(key, "tutorial.callout.step1.guide_btn") == 0) {
    return zh ? QString::fromUtf8("界面導覽\n界面導覽。")
              : QString("GUIDE Button\nOpen/close the interactive guide anytime without changing current mission setup.");
  }
  if (std::strcmp(key, "tutorial.callout.step1.lang_btn") == 0) {
    return zh ? QString::fromUtf8("English/中文\n界面語言（English/中文）。")
              : QString("Language Toggle\nSwitch UI language; tutorial text updates immediately.");
  }

  if (std::strcmp(key, "tutorial.callout.step2.osm_llh") == 0) {
    return zh ? QString::fromUtf8("當下座標\n執行當下的接收機座標。")
              : QString("Mission Coordinates\nDisplays mission start and receiver position for quick validation.");
  }
  if (std::strcmp(key, "tutorial.callout.step2.smart_path") == 0) {
    return zh ? QString::fromUtf8("雙擊：智慧道路預覽\n依真實 OSM 可通行道路規劃，不是直線連接。")
              : QString("Double Click: Smart Road Preview\nRoutes over real drivable OSM roads, not a direct straight line.");
  }
  if (std::strcmp(key, "tutorial.callout.step2.straight_path") == 0) {
    return zh ? QString::fromUtf8("左鍵：直線預覽\n從起點直接連到終點，不依附道路。")
              : QString("Left Click: Straight Preview\nDirect line from start to end without road constraints.");
  }
  if (std::strcmp(key, "tutorial.callout.step2.mouse_hint") == 0) {
    return zh ? QString::fromUtf8("右鍵：確認路徑\n確認目前預覽結果並加入路徑佇列。")
              : QString("Right Click: Confirm Path\nConfirm the current preview and add it to the mission path queue.");
  }

  if (std::strcmp(key, "tutorial.callout.step3.sky_g") == 0) {
    return zh ? QString::fromUtf8("GPS\nGPS衛星當下座標")
              : QString("GPS\nCurrent GPS satellite positions.");
  }
  if (std::strcmp(key, "tutorial.callout.step3.sky_c") == 0) {
    return zh ? QString::fromUtf8("BDS\nBDS衛星當下座標")
              : QString("BDS\nCurrent BDS satellite positions.");
  }

  if (std::strcmp(key, "tutorial.callout.step4.wave_1") == 0) {
    return zh ? QString::fromUtf8("頻譜面板\n訊號在頻率維度上的能量分佈。")
              : QString("Spectrum Panel\nPower distribution of the signal across frequency.");
  }
  if (std::strcmp(key, "tutorial.callout.step4.wave_2") == 0) {
    return zh ? QString::fromUtf8("瀑布圖面板\n訊號在時間與頻率維度上的能量強度演變。")
              : QString("Waterfall Panel\nSignal intensity evolution across time and frequency.");
  }
  if (std::strcmp(key, "tutorial.callout.step4.wave_3") == 0) {
    return zh ? QString::fromUtf8("時域波形面板\n訊號在時間維度上的波形變化。")
              : QString("Time-Domain Panel\nWaveform variation of the signal over time.");
  }
  if (std::strcmp(key, "tutorial.callout.step4.wave_4") == 0) {
    return zh ? QString::fromUtf8("星座圖面板\n訊號在複數平面上的相位與振幅分佈。")
              : QString("Constellation Panel\nSignal phase and amplitude distribution on the I/Q plane.");
  }

  if (std::strcmp(key, "tutorial.callout.step5.sig_bdt_gpst") == 0) {
    return zh ? QString::fromUtf8("BDT / GPST\n北斗/GPS系統時間")
              : QString("BDT / GPST\nBeiDou and GPS system time references.");
  }
  if (std::strcmp(key, "tutorial.callout.step5.sig_rnx") == 0) {
    return zh ? QString::fromUtf8("RNX\n北斗/GPS系統參考星曆檔名")
              : QString("RNX\nReference ephemeris file names for BDS/GPS systems.");
  }
  if (std::strcmp(key, "tutorial.callout.step5.sig_tab_simple") == 0) {
    return zh ? QString::fromUtf8("基礎頁面設定")
              : QString("Basic Page Settings\nMain controls for standard operation.");
  }
  if (std::strcmp(key, "tutorial.callout.step5.sig_tab_detail") == 0) {
    return zh ? QString::fromUtf8("進階頁面設定")
              : QString("Advanced Page Settings\nExtended controls for fine-grained setup.");
  }

  if (std::strcmp(key, "tutorial.callout.step6.sig_interfere") == 0) {
    return zh ? QString::fromUtf8("攻擊模式\n選擇欺騙、定位彈弓或干擾模式")
              : QString("Attack Mode\nSelect spoofing, crossbow, or jamming mode.");
  }
  if (std::strcmp(key, "tutorial.callout.step6.sig_system") == 0) {
    return zh ? QString::fromUtf8("系統選擇\n選擇啟用的衛星系統")
              : QString("System Selection\nChoose which satellite system(s) are enabled.");
  }
  if (std::strcmp(key, "tutorial.callout.step6.sig_fs") == 0) {
    return zh ? QString::fromUtf8("取樣率\n每秒對連續訊號進行測量與記錄的次數")
              : QString("Sample Rate\nNumber of measurements recorded per second for continuous signals.");
  }
  if (std::strcmp(key, "tutorial.callout.step6.sig_tx") == 0) {
    return zh ? QString::fromUtf8("發射增益\n射頻發射增益")
              : QString("Transmit Gain\nRF transmission gain level.");
  }
  if (std::strcmp(key, "tutorial.callout.step6.sig_start") == 0) {
    return zh ? QString::fromUtf8("開始執行")
              : QString("Start Execution\nBegin runtime signal generation/transmission.");
  }
  if (std::strcmp(key, "tutorial.callout.step6.sig_exit") == 0) {
    return zh ? QString::fromUtf8("中止系統")
              : QString("Abort System\nStop and leave the current execution flow.");
  }

  if (std::strcmp(key, "tutorial.callout.step7.detail_sats") == 0) {
    return zh ? QString::fromUtf8("衛星\n可視化衛星")
              : QString("Satellites\nVisualized satellite candidates.");
  }
  if (std::strcmp(key, "tutorial.callout.step7.gain_slider") == 0) {
    return zh ? QString::fromUtf8("訊號增益\n基礎訊號振幅縮放比例")
              : QString("Signal Gain\nBase scaling ratio of the signal amplitude.");
  }
  if (std::strcmp(key, "tutorial.callout.step7.cn0_slider") == 0) {
    return zh ? QString::fromUtf8("目標 C/N0\n目標訊號品質")
              : QString("Target C/N0\nTarget signal quality level.");
  }
  if (std::strcmp(key, "tutorial.callout.step7.path_v_slider") == 0) {
    return zh ? QString::fromUtf8("路徑時速上限\n路徑中段所保持的速度")
              : QString("Path Speed Limit\nSpeed maintained in the middle segment of the path.");
  }
  if (std::strcmp(key, "tutorial.callout.step7.path_a_slider") == 0) {
    return zh ? QString::fromUtf8("路徑加速度\n路徑頭段尾段的加速度")
              : QString("Path Acceleration\nAcceleration used in the start and end path segments.");
  }
  if (std::strcmp(key, "tutorial.callout.step7.prn_slider") == 0) {
    return zh ? QString::fromUtf8("PRN 選擇\n單一衛星編號選擇")
              : QString("PRN Selection\nSelect a single satellite ID.");
  }
  if (std::strcmp(key, "tutorial.callout.step7.ch_slider") == 0) {
    return zh ? QString::fromUtf8("最大通道數\n模擬的衛星通道數量上限(1-16)")
              : QString("Maximum Channels\nUpper limit of simulated satellite channels (1-16).");
  }

  if (std::strcmp(key, "tutorial.callout.step8.sw_fmt") == 0) {
    return zh ? QString::fromUtf8("訊號格式\n傳輸的訊號格式位元數(16-bit, 8-bit)")
              : QString("Signal Format\nBit depth of transmitted signal format (16-bit, 8-bit).");
  }
  if (std::strcmp(key, "tutorial.callout.step8.sw_mode") == 0) {
    return zh ? QString::fromUtf8("衛星範圍\n採用的衛星編號範圍(37-北斗二號，63-北斗三號)")
              : QString("Satellite Range\nSatellite ID range in use (37 for BDS-2, 63 for BDS-3).");
  }
  if (std::strcmp(key, "tutorial.callout.step8.tg_meo") == 0) {
    return zh ? QString::fromUtf8("MEO\n只啟用 MEO 衛星")
              : QString("MEO\nEnable only MEO satellites.");
  }
  if (std::strcmp(key, "tutorial.callout.step8.tg_iono") == 0) {
    return zh ? QString::fromUtf8("電離層\n啟用電離層效應模型")
              : QString("Ionosphere\nEnable the ionospheric effect model.");
  }
  if (std::strcmp(key, "tutorial.callout.step8.tg_clk") == 0) {
    return zh ? QString::fromUtf8("外部時鐘\n使用外部 USRP 參考時鐘")
              : QString("External Reference Clock\nUse an external USRP reference clock.");
  }


  if (std::strcmp(key, "scene.sys_gps") == 0) {
    return QString("GPS");
  }
  if (std::strcmp(key, "scene.sys_mixed") == 0) {
    return QString("BDS+GPS");
  }
  if (std::strcmp(key, "scene.sys_bds") == 0) {
    return zh ? QString::fromUtf8("非靜止軌道北斗") : QString("Non-GEO BDS");
  }
  if (std::strcmp(key, "scene.jam_fmt") == 0) {
    return zh ? QString::fromUtf8("JAM %1（偽合法 PRN）")
              : QString("JAM %1 (Pseudo-Legit PRN)");
  }
  if (std::strcmp(key, "scene.sat_fmt") == 0) {
    return zh ? QString::fromUtf8("%1 衛星數: %2") : QString("%1 Satellites: %2");
  }
  if (std::strcmp(key, "scene.sat_fallback_fmt") == 0) {
    return zh ? QString::fromUtf8("%1 衛星數: %2（混合備援）")
              : QString("%1 Satellites: %2 (fallback mixed)");
  }

  return QString::fromUtf8(key);
}
