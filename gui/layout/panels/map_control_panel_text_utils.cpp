#include "gui/layout/panels/map_control_panel_text_utils.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

QString map_control_short_base_name(const QString &path) {
  const int slash_pos = std::max(path.lastIndexOf('/'), path.lastIndexOf('\\'));
  return (slash_pos >= 0) ? path.mid(slash_pos + 1) : path;
}

QString map_control_compact_rnx_suffix_text(const QString &name) {
  const QString base = map_control_short_base_name(name).trimmed();
  if (base.isEmpty())
    return QString("N/A");

  const QString lower = base.toLower();
  const int rnx_pos = lower.lastIndexOf(".rnx");
  const QString ext = (rnx_pos >= 0) ? base.mid(rnx_pos) : QString(".rnx");

  const QRegularExpression re("(\\d{7,})");
  QRegularExpressionMatchIterator it = re.globalMatch(base);
  QString best;
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    const QString token = m.captured(1);
    if (token.size() > best.size())
      best = token;
  }

  if (!best.isEmpty()) {
    const QString core = (best.size() > 11) ? best.right(11) : best;
    return QString("...%1...%2").arg(core, ext.toLower());
  }

  if (base.size() <= 16)
    return base;
  return QString("...%1").arg(base.right(13));
}

QString map_control_week_sow_compact(int week, double sow) {
  const double clamped_sow = std::max(0.0, sow);
  const long long sow_tenths =
      (long long)std::llround(clamped_sow * 10.0);
  const long long sow_int = sow_tenths / 10;
  const int sow_frac = (int)(sow_tenths % 10);
  const QString sow_text =
      QString("%1.%2").arg(sow_int, 5, 10, QChar('0')).arg(sow_frac);
  return QString("W%1 SOW %2").arg(week, 4, 10, QChar('0')).arg(sow_text);
}
