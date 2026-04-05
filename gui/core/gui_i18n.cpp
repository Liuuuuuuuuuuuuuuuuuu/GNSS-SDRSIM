#include "gui/core/gui_i18n.h"

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
    return zh ? QString::fromUtf8("搜尋地點並按 Enter")
              : QString("Search a location and press Enter");
  }
  if (std::strcmp(key, "search.no_match") == 0) {
    return zh ? QString::fromUtf8("找不到符合搜尋條件的地點。")
              : QString("No place matched your search text.");
  }
  if (std::strcmp(key, "alert.spoof_rinex_required") == 0) {
    return zh ? QString::fromUtf8("SPOOF 模式需要有效的 RINEX 檔案，目前僅可使用 JAM。")
              : QString("SPOOF mode requires a valid RINEX file. JAM only is available right now.");
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
    return zh ? QString::fromUtf8("無法復原：最後一段已執行或佇列為空")
              : QString("Cannot undo: last segment is already executing or queue is empty");
  }
  if (std::strcmp(key, "path.undo_ok") == 0) {
    return zh ? QString::fromUtf8("已復原最後一段佇列路徑")
              : QString("Last queued segment has been undone");
  }

  if (std::strcmp(key, "panel.signal_settings") == 0) {
    return zh ? QString::fromUtf8("訊號設定") : QString("Signal Settings");
  }
  if (std::strcmp(key, "panel.week_sow") == 0) {
    return zh ? QString::fromUtf8("第 %1 週 SOW %2") : QString("Week %1 SOW %2");
  }
  if (std::strcmp(key, "panel.rnx") == 0) {
    return QString("RNX | %1");
  }
  if (std::strcmp(key, "panel.bdt") == 0) {
    return QString("BDT  | %1");
  }
  if (std::strcmp(key, "panel.gpst") == 0) {
    return QString("GPST | %1");
  }
  if (std::strcmp(key, "panel.sats_none") == 0) {
    return zh ? QString::fromUtf8("衛星 | 無") : QString("SATS | none");
  }
  if (std::strcmp(key, "panel.sats_prefix") == 0) {
    return zh ? QString::fromUtf8("衛星 |") : QString("SATS |");
  }
  if (std::strcmp(key, "panel.sats_more") == 0) {
    return zh ? QString::fromUtf8(" ...（共 %1 顆）") : QString(" ... (%1)");
  }

  if (std::strcmp(key, "tab.simple") == 0) {
    return zh ? QString::fromUtf8("簡易") : QString("SIMPLE");
  }
  if (std::strcmp(key, "tab.detail") == 0) {
    return zh ? QString::fromUtf8("進階") : QString("DETAIL");
  }
  if (std::strcmp(key, "label.interfere") == 0) {
    return zh ? QString::fromUtf8("模式") : QString("INTERFERE");
  }
  if (std::strcmp(key, "label.spoof") == 0) {
    return zh ? QString::fromUtf8("欺騙") : QString("SPOOF");
  }
  if (std::strcmp(key, "label.jam") == 0) {
    return zh ? QString::fromUtf8("干擾") : QString("JAM");
  }
  if (std::strcmp(key, "label.system") == 0) {
    return zh ? QString::fromUtf8("系統") : QString("SYSTEM");
  }
  if (std::strcmp(key, "label.fs") == 0) {
    return zh ? QString::fromUtf8("FS（頻率）") : QString("FS (Frequency)");
  }
  if (std::strcmp(key, "label.signal_gain") == 0) {
    return zh ? QString::fromUtf8("訊號增益") : QString("Signal Gain");
  }
  if (std::strcmp(key, "label.tx_gain") == 0) {
    return zh ? QString::fromUtf8("TX（發射增益）") : QString("TX (Transmit Gain)");
  }
  if (std::strcmp(key, "label.target_cn0") == 0) {
    return zh ? QString::fromUtf8("目標 C/N0") : QString("Target C/N0");
  }
  if (std::strcmp(key, "label.seed") == 0) {
    return zh ? QString::fromUtf8("種子") : QString("Seed");
  }
  if (std::strcmp(key, "label.prn_select") == 0) {
    return zh ? QString::fromUtf8("PRN 選擇") : QString("PRN Select");
  }
  if (std::strcmp(key, "label.path_vmax") == 0) {
    return zh ? QString::fromUtf8("路徑最高速") : QString("Path Vmax");
  }
  if (std::strcmp(key, "label.path_acc") == 0) {
    return zh ? QString::fromUtf8("路徑加速度") : QString("Path Acc");
  }
  if (std::strcmp(key, "label.max_ch") == 0) {
    return zh ? QString::fromUtf8("最大通道") : QString("Max CH");
  }
  if (std::strcmp(key, "label.mode") == 0) {
    return QString("MODE");
  }
  if (std::strcmp(key, "label.format") == 0) {
    return zh ? QString::fromUtf8("格式") : QString("FORMAT");
  }
  if (std::strcmp(key, "label.start") == 0) {
    return zh ? QString::fromUtf8("開始") : QString("START");
  }
  if (std::strcmp(key, "label.exit") == 0) {
    return zh ? QString::fromUtf8("離開") : QString("EXIT");
  }
  if (std::strcmp(key, "value.prn") == 0) {
    return QString("PRN %1");
  }

  if (std::strcmp(key, "osm.back") == 0) {
    return zh ? QString::fromUtf8("復原") : QString("BACK");
  }
  if (std::strcmp(key, "osm.nfz_on") == 0) {
    return zh ? QString::fromUtf8("禁飛 開") : QString("NFZ ON");
  }
  if (std::strcmp(key, "osm.nfz_off") == 0) {
    return zh ? QString::fromUtf8("禁飛 關") : QString("NFZ OFF");
  }
  if (std::strcmp(key, "osm.light") == 0) {
    return zh ? QString::fromUtf8("亮色") : QString("LIGHT");
  }
  if (std::strcmp(key, "osm.dark") == 0) {
    return zh ? QString::fromUtf8("暗色") : QString("DARK");
  }
  if (std::strcmp(key, "osm.guide_on") == 0) {
    return zh ? QString::fromUtf8("導覽 開") : QString("GUIDE ON");
  }
  if (std::strcmp(key, "osm.guide_off") == 0) {
    return zh ? QString::fromUtf8("導覽 關") : QString("GUIDE OFF");
  }
  if (std::strcmp(key, "osm.return") == 0) {
    return zh ? QString::fromUtf8("返回") : QString("RETURN");
  }
  if (std::strcmp(key, "osm.stop") == 0) {
    return zh ? QString::fromUtf8("停止") : QString("STOP");
  }
  if (std::strcmp(key, "osm.run_time") == 0) {
    return zh ? QString::fromUtf8("執行時間") : QString("RUN TIME");
  }
  if (std::strcmp(key, "osm.init_time") == 0) {
    return zh ? QString::fromUtf8("初始化時間") : QString("INIT TIME");
  }
  if (std::strcmp(key, "osm.legend_restricted") == 0) {
    return zh ? QString::fromUtf8("限制區") : QString("Restricted");
  }
  if (std::strcmp(key, "osm.legend_auth_warn") == 0) {
    return zh ? QString::fromUtf8("授權/警示") : QString("Auth/Warn");
  }
  if (std::strcmp(key, "osm.lang_btn") == 0) {
    return zh ? QString::fromUtf8("語言:中文") : QString("LANG:EN");
  }

  if (std::strcmp(key, "status.current") == 0) {
    return zh ? QString::fromUtf8("目前 %1, %2") : QString("Current %1, %2");
  }
  if (std::strcmp(key, "status.current_na") == 0) {
    return zh ? QString::fromUtf8("目前 N/A") : QString("Current N/A");
  }
  if (std::strcmp(key, "status.start_llh") == 0) {
    return zh ? QString::fromUtf8("起始 LLH %1, %2, %3 | %4")
              : QString("Start LLH %1, %2, %3 | %4");
  }
  if (std::strcmp(key, "status.zoom") == 0) {
    return zh ? QString::fromUtf8("OpenStreetMap（拖曳平移、滾輪縮放）| 縮放 %1")
              : QString("OpenStreetMap (drag to pan, wheel to zoom) | Zoom %1");
  }
  if (std::strcmp(key, "status.new_user_tip") == 0) {
    return zh ? QString::fromUtf8("新手提示：點右上角導覽開關可開啟逐步教學")
              : QString("New user tip: click GUIDE OFF at top-right to open step-by-step tutorial");
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

  if (std::strcmp(key, "tutorial.card.title") == 0) {
    return zh ? QString::fromUtf8("教學 %1 / %2  %3")
              : QString("Tutorial %1 / %2  %3");
  }
  if (std::strcmp(key, "tutorial.card.focus") == 0) {
    return zh
               ? QString::fromUtf8("\n\n焦點 %1/%2：%3\n%4\n高亮框即為目前目標控制元件。")
               : QString("\n\nFocus %1/%2: %3\n%4\nHighlighted frame marks the target control.");
  }
  if (std::strcmp(key, "tutorial.card.page") == 0) {
    return zh ? QString::fromUtf8("頁面 %1 / %2") : QString("Page %1 / %2");
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
  if (std::strcmp(key, "tutorial.btn.close") == 0) {
    return zh ? QString::fromUtf8("關閉") : QString("CLOSE");
  }

  if (std::strcmp(key, "tutorial.step.title.0") == 0) {
    return zh ? QString::fromUtf8("歡迎") : QString("Welcome");
  }
  if (std::strcmp(key, "tutorial.step.title.1") == 0) {
    return zh ? QString::fromUtf8("步驟 1：選擇起點")
              : QString("Step 1: Pick Start Point");
  }
  if (std::strcmp(key, "tutorial.step.title.2") == 0) {
    return zh ? QString::fromUtf8("步驟 2：SIMPLE 分頁")
              : QString("Step 2: SIMPLE Tab");
  }
  if (std::strcmp(key, "tutorial.step.title.3") == 0) {
    return zh ? QString::fromUtf8("步驟 3：SIMPLE 控制面板")
              : QString("Step 3: SIMPLE Control Panel");
  }
  if (std::strcmp(key, "tutorial.step.title.4") == 0) {
    return zh ? QString::fromUtf8("步驟 4：GNSS-SDRSIM（右上）")
              : QString("Step 4: GNSS-SDRSIM (Top-Right)");
  }
  if (std::strcmp(key, "tutorial.step.title.5") == 0) {
    return zh ? QString::fromUtf8("步驟 5：切換到 DETAIL")
              : QString("Step 5: Switch to DETAIL");
  }
  if (std::strcmp(key, "tutorial.step.title.6") == 0) {
    return zh ? QString::fromUtf8("步驟 6：DETAIL 控制面板")
              : QString("Step 6: DETAIL Control Panel");
  }
  if (std::strcmp(key, "tutorial.step.title.7") == 0) {
    return zh ? QString::fromUtf8("步驟 7：更多 DETAIL 控制")
              : QString("Step 7: More DETAIL Controls");
  }
  if (std::strcmp(key, "tutorial.step.title.8") == 0) {
    return zh ? QString::fromUtf8("步驟 8：開始傳輸")
              : QString("Step 8: Start Transmission");
  }
  if (std::strcmp(key, "tutorial.step.title.9") == 0) {
    return zh ? QString::fromUtf8("步驟 9：頻譜面板")
              : QString("Step 9: Spectrum Panel");
  }
  if (std::strcmp(key, "tutorial.step.title.10") == 0) {
    return zh ? QString::fromUtf8("步驟 10：瀑布圖面板")
              : QString("Step 10: Waterfall Panel");
  }
  if (std::strcmp(key, "tutorial.step.title.11") == 0) {
    return zh ? QString::fromUtf8("步驟 11：時域面板")
              : QString("Step 11: Time-Domain Panel");
  }
  if (std::strcmp(key, "tutorial.step.title.12") == 0) {
    return zh ? QString::fromUtf8("步驟 12：星座圖面板")
              : QString("Step 12: Constellation Panel");
  }
  if (std::strcmp(key, "tutorial.step.title.13") == 0) {
    return zh ? QString::fromUtf8("步驟 13：停止與重設")
              : QString("Step 13: Stop and Reset");
  }
  if (std::strcmp(key, "tutorial.step.title.14") == 0) {
    return zh ? QString::fromUtf8("步驟 14：離開") : QString("Step 14: Exit");
  }

  if (std::strcmp(key, "tutorial.step.body.0") == 0) {
    return zh
               ? QString::fromUtf8("教學為可選功能，預設為關閉。\n可用「導覽 關 / 導覽 開」顯示或隱藏此覆蓋層。")
               : QString("Guide is optional and OFF by default.\nUse GUIDE OFF / GUIDE ON to show or hide this overlay.");
  }
  if (std::strcmp(key, "tutorial.step.body.1") == 0) {
    return zh
               ? QString::fromUtf8("左側地圖：左鍵點擊設定起始 LLH。\n有有效起點後，START 才會啟用。")
               : QString("Left map: left-click to set Start LLH.\nSTART becomes available after a valid start point.");
  }
  if (std::strcmp(key, "tutorial.step.body.2") == 0) {
    return zh
               ? QString::fromUtf8("SIMPLE 分頁：入門頁面。\n初次操作建議先從這裡開始，最快達到 START。")
               : QString("SIMPLE tab: beginner page.\nUse this first for the fastest path to START.");
  }
  if (std::strcmp(key, "tutorial.step.body.3") == 0) {
    return zh
               ? QString::fromUtf8("SIMPLE 控制：SYS、Fs、Tx Gain。\n高亮會依序指示各控制元件。\nSYS 與 Fs 需相容。")
               : QString("SIMPLE controls: SYS, Fs, Tx Gain.\nHighlight shows each control in order.\nSYS and Fs must be compatible.");
  }
  if (std::strcmp(key, "tutorial.step.body.4") == 0) {
    return zh
               ? QString::fromUtf8("右上 GNSS-SDRSIM 面板：顯示衛星幾何與接收機標記。")
               : QString("Top-right GNSS-SDRSIM panel: shows satellite geometry and receiver marker.");
  }
  if (std::strcmp(key, "tutorial.step.body.5") == 0) {
    return zh ? QString::fromUtf8("切換到 DETAIL 分頁以使用進階控制。")
              : QString("Switch to DETAIL tab for advanced controls.");
  }
  if (std::strcmp(key, "tutorial.step.body.6") == 0) {
    return zh
               ? QString::fromUtf8("DETAIL 數值控制：Gain、CN0、PRN、Seed、CH。\n高亮會循環提示。")
               : QString("DETAIL numeric controls: Gain, CN0, PRN, Seed, CH.\nHighlight cycles through them.");
  }
  if (std::strcmp(key, "tutorial.step.body.7.detail") == 0) {
    return zh
               ? QString::fromUtf8("DETAIL 開關：MODE、FORMAT、MEO、IONO、EXT CLK。\n用於行為與硬體選項設定。")
               : QString("DETAIL switches: MODE, FORMAT, MEO, IONO, EXT CLK.\nUse these for behavior and hardware options.");
  }
  if (std::strcmp(key, "tutorial.step.body.7.simple") == 0) {
    return zh
               ? QString::fromUtf8("元件：額外 DETAIL 控制。\nDETAIL 提供比 SIMPLE 更多操作。\n例如：CH、CN0、PRN、Seed。")
               : QString("Component: extra DETAIL controls.\nDETAIL gives you more operations than SIMPLE.\nExamples: CH, CN0, PRN, and Seed.");
  }
  if (std::strcmp(key, "tutorial.step.body.8.running") == 0) {
    return zh
               ? QString::fromUtf8("系統已在執行中。\n目前高亮元件：STOP SIMULATION 按鈕。\n可用它停止目前傳輸。")
               : QString("You are already running.\nCurrent component highlighted: STOP SIMULATION button.\nUse it to stop current transmission.");
  }
  if (std::strcmp(key, "tutorial.step.body.8.idle") == 0) {
    return zh ? QString::fromUtf8("按下 START 開始產生/傳輸。")
              : QString("Press START to begin generation/transmission.");
  }
  if (std::strcmp(key, "tutorial.step.body.9") == 0) {
    return zh ? QString::fromUtf8("頻譜面板：顯示頻率上的功率分布。")
              : QString("Spectrum panel: power over frequency.");
  }
  if (std::strcmp(key, "tutorial.step.body.10") == 0) {
    return zh ? QString::fromUtf8("瀑布圖面板：顯示頻率隨時間的歷史變化。")
              : QString("Waterfall panel: frequency history over time.");
  }
  if (std::strcmp(key, "tutorial.step.body.11") == 0) {
    return zh ? QString::fromUtf8("時域面板：顯示隨時間變化的波形。")
              : QString("Time-domain panel: waveform over time.");
  }
  if (std::strcmp(key, "tutorial.step.body.12") == 0) {
    return zh ? QString::fromUtf8("星座圖面板：顯示 I/Q 分布與穩定度。")
              : QString("Constellation panel: I/Q distribution and stability.");
  }
  if (std::strcmp(key, "tutorial.step.body.13") == 0) {
    return zh
               ? QString::fromUtf8("按下 STOP SIMULATION 可中止執行並回到待機。")
               : QString("Press STOP SIMULATION to abort run and return to standby.");
  }
  if (std::strcmp(key, "tutorial.step.body.14") == 0) {
    return zh ? QString::fromUtf8("按下 EXIT 關閉程式。")
              : QString("Press EXIT to close the program.");
  }

  if (std::strcmp(key, "tutorial.spot.3.0.name") == 0) {
    return QString("SYS");
  }
  if (std::strcmp(key, "tutorial.spot.3.0.desc") == 0) {
    return zh
               ? QString::fromUtf8("選擇訊號系統：BDS、BDS+GPS 或 GPS。")
               : QString("Choose signal family: BDS, BDS+GPS, or GPS.");
  }
  if (std::strcmp(key, "tutorial.spot.3.1.name") == 0) {
    return QString("Fs");
  }
  if (std::strcmp(key, "tutorial.spot.3.1.desc") == 0) {
    return zh
               ? QString::fromUtf8("設定取樣率，需符合 SYS 的最低需求。")
               : QString("Set sample rate. It must match the minimum required by SYS.");
  }
  if (std::strcmp(key, "tutorial.spot.3.2.name") == 0) {
    return zh ? QString::fromUtf8("Tx 增益") : QString("Tx Gain");
  }
  if (std::strcmp(key, "tutorial.spot.3.2.desc") == 0) {
    return zh
               ? QString::fromUtf8("設定 RF 輸出強度，建議先低後高調整。")
               : QString("Set RF output strength. Start lower, then increase if needed.");
  }

  if (std::strcmp(key, "tutorial.spot.6.0.name") == 0) {
    return zh ? QString::fromUtf8("增益") : QString("Gain");
  }
  if (std::strcmp(key, "tutorial.spot.6.0.desc") == 0) {
    return zh
               ? QString::fromUtf8("細部調整接收端強度，配合模擬配置。")
               : QString("Fine receiver-side strength tuning for simulation profile.");
  }
  if (std::strcmp(key, "tutorial.spot.6.1.name") == 0) {
    return QString("CN0");
  }
  if (std::strcmp(key, "tutorial.spot.6.1.desc") == 0) {
    return zh
               ? QString::fromUtf8("目標載噪比，越高代表訊號越乾淨。")
               : QString("Target carrier-to-noise level. Higher means cleaner signal.");
  }
  if (std::strcmp(key, "tutorial.spot.6.2.name") == 0) {
    return QString("PRN");
  }
  if (std::strcmp(key, "tutorial.spot.6.2.desc") == 0) {
    return zh
               ? QString::fromUtf8("選擇衛星 PRN，適合聚焦測試場景。")
               : QString("Choose satellite PRN for focused test scenarios.");
  }
  if (std::strcmp(key, "tutorial.spot.6.3.name") == 0) {
    return zh ? QString::fromUtf8("種子") : QString("Seed");
  }
  if (std::strcmp(key, "tutorial.spot.6.3.desc") == 0) {
    return zh
               ? QString::fromUtf8("隨機種子，讓測試行為可重現。")
               : QString("Random seed for reproducible test behavior.");
  }
  if (std::strcmp(key, "tutorial.spot.6.4.name") == 0) {
    return QString("CH");
  }
  if (std::strcmp(key, "tutorial.spot.6.4.desc") == 0) {
    return zh
               ? QString::fromUtf8("限制啟用通道數，適合負載與邊界測試。")
               : QString("Limit active channels. Useful for load and edge-case tests.");
  }

  if (std::strcmp(key, "tutorial.spot.7.0.name") == 0) {
    return QString("MODE");
  }
  if (std::strcmp(key, "tutorial.spot.7.0.desc") == 0) {
    return zh
               ? QString::fromUtf8("選擇本次執行的衛星選取行為。")
               : QString("Choose satellite selection behavior for this run.");
  }
  if (std::strcmp(key, "tutorial.spot.7.1.name") == 0) {
    return zh ? QString::fromUtf8("格式") : QString("FORMAT");
  }
  if (std::strcmp(key, "tutorial.spot.7.1.desc") == 0) {
    return zh
               ? QString::fromUtf8("選擇輸出取樣格式：SHORT 或 BYTE。")
               : QString("Choose output sample format: SHORT or BYTE.");
  }
  if (std::strcmp(key, "tutorial.spot.7.2.name") == 0) {
    return QString("MEO");
  }
  if (std::strcmp(key, "tutorial.spot.7.2.desc") == 0) {
    return zh
               ? QString::fromUtf8("依需求切換為僅 MEO 模式。")
               : QString("Toggle MEO-only behavior when your test needs it.");
  }
  if (std::strcmp(key, "tutorial.spot.7.3.name") == 0) {
    return QString("IONO");
  }
  if (std::strcmp(key, "tutorial.spot.7.3.desc") == 0) {
    return zh
               ? QString::fromUtf8("切換電離層效應模型開或關。")
               : QString("Toggle ionospheric effect model on or off.");
  }
  if (std::strcmp(key, "tutorial.spot.7.4.name") == 0) {
    return QString("EXT CLK");
  }
  if (std::strcmp(key, "tutorial.spot.7.4.desc") == 0) {
    return zh
               ? QString::fromUtf8("切換外部時鐘來源，供硬體同步使用。")
               : QString("Toggle external clock source usage for hardware sync.");
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
