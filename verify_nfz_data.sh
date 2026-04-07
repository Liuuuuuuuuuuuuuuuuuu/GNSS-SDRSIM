#!/bin/bash

# NFZ API 數據驗證腳本
# 用途：驗證 test_nfz_sample.json 的格式是否正確

set -e

echo "🔍 NFZ 數據格式驗證工具"
echo "=================================================="
echo ""

JSON_FILE="${1:-test_nfz_sample.json}"

if [ ! -f "$JSON_FILE" ]; then
    echo "❌ 錯誤：找不到文件 $JSON_FILE"
    exit 1
fi

echo "📋 分析文件：$JSON_FILE"
echo ""

# 驗證 JSON 格式
if ! jq empty "$JSON_FILE" 2>/dev/null; then
    echo "❌ JSON 格式無效"
    exit 1
fi

echo "✅ JSON 格式有效"
echo ""

# 統計禁航區
AREA_COUNT=$(jq '.data.areas | length' "$JSON_FILE")
echo "📍 禁航區數量：$AREA_COUNT"
echo ""

# 逐個顯示禁航區詳情
jq -r '.data.areas[] | "  • " + .name + " (Level: " + (.level|tostring) + ")"' "$JSON_FILE" | while read line; do
    echo "$line"
done

echo ""
echo "=================================================="
echo "💡 數據驗證完成"
echo ""
echo "下一步建議："
echo "1. 在瀏覽器開發者工具中攔截真實的 DJI API 回應"
echo "2. 將完整的 JSON 替換測試文件內容"
echo "3. 在地圖上驗證渲染效果"
echo ""
