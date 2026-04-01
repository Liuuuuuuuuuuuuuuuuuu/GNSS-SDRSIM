#include "gui/geo_io.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

double wrap_lon_deg(double lon)
{
    double x = lon;
    while (x < -180.0) x += 360.0;
    while (x >= 180.0) x -= 360.0;
    return x;
}

double wrap_lon_delta_deg(double dlon)
{
    double d = dlon;
    while (d > 180.0) d -= 360.0;
    while (d < -180.0) d += 360.0;
    return d;
}

double distance_m_approx(double lat0_deg, double lon0_deg, double lat1_deg, double lon1_deg)
{
    double lat0 = lat0_deg * M_PI / 180.0;
    double lat1 = lat1_deg * M_PI / 180.0;
    double dlat = lat1 - lat0;
    double dlon = (lon1_deg - lon0_deg) * M_PI / 180.0;
    double mean_lat = 0.5 * (lat0 + lat1);
    double x = dlon * std::cos(mean_lat) * 6371000.0;
    double y = dlat * 6371000.0;
    return std::sqrt(x * x + y * y);
}

static uint32_t read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return ((uint32_t)p[3] << 24) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[1] << 8) |
           (uint32_t)p[0];
}

static double read_f64_le(const uint8_t *p)
{
    uint64_t v = ((uint64_t)p[7] << 56) |
                 ((uint64_t)p[6] << 48) |
                 ((uint64_t)p[5] << 40) |
                 ((uint64_t)p[4] << 32) |
                 ((uint64_t)p[3] << 24) |
                 ((uint64_t)p[2] << 16) |
                 ((uint64_t)p[1] << 8) |
                 (uint64_t)p[0];
    double d = 0.0;
    std::memcpy(&d, &v, sizeof(d));
    return d;
}

bool load_land_shapefile(const char *shp_path, std::vector<std::vector<LonLat>> &parts_out)
{
    parts_out.clear();

    std::ifstream in(shp_path, std::ios::binary);
    if (!in) return false;

    uint8_t header[100];
    in.read((char *)header, sizeof(header));
    if (!in) return false;

    uint32_t file_code = read_u32_be(header + 0);
    if (file_code != 9994) return false;

    while (true) {
        uint8_t rec_head[8];
        in.read((char *)rec_head, sizeof(rec_head));
        if (!in) break;

        uint32_t content_len_words = read_u32_be(rec_head + 4);
        size_t content_len_bytes = (size_t)content_len_words * 2u;
        if (content_len_bytes < 4) {
            in.seekg((std::streamoff)content_len_bytes, std::ios::cur);
            if (!in) break;
            continue;
        }

        std::vector<uint8_t> rec(content_len_bytes);
        in.read((char *)rec.data(), (std::streamsize)content_len_bytes);
        if (!in) break;

        uint32_t shape_type = read_u32_le(rec.data() + 0);
        if (shape_type != 5 && shape_type != 15 && shape_type != 25) continue;
        if (content_len_bytes < 44) continue;

        uint32_t num_parts = read_u32_le(rec.data() + 36);
        uint32_t num_points = read_u32_le(rec.data() + 40);

        size_t parts_off = 44;
        size_t points_off = parts_off + (size_t)num_parts * 4u;
        size_t need = points_off + (size_t)num_points * 16u;
        if (need > content_len_bytes || num_parts == 0 || num_points == 0) continue;

        std::vector<uint32_t> part_idx(num_parts);
        for (uint32_t i = 0; i < num_parts; ++i) {
            part_idx[i] = read_u32_le(rec.data() + parts_off + (size_t)i * 4u);
        }

        for (uint32_t i = 0; i < num_parts; ++i) {
            uint32_t begin = part_idx[i];
            uint32_t end = (i + 1 < num_parts) ? part_idx[i + 1] : num_points;
            if (begin >= end || end > num_points) continue;

            std::vector<LonLat> ring;
            ring.reserve((size_t)(end - begin));
            for (uint32_t j = begin; j < end; ++j) {
                const uint8_t *pt = rec.data() + points_off + (size_t)j * 16u;
                double lon = read_f64_le(pt + 0);
                double lat = read_f64_le(pt + 8);
                ring.push_back({lon, lat});
            }
            if (ring.size() >= 2) parts_out.push_back(std::move(ring));
        }
    }

    return !parts_out.empty();
}

int lon_to_x(double lon_deg, int width)
{
    double lon = lon_deg;
    while (lon < -180.0) lon += 360.0;
    while (lon >= 180.0) lon -= 360.0;
    double x = (lon + 180.0) / 360.0 * (double)(width - 1);
    if (x < 0.0) x = 0.0;
    if (x > (double)(width - 1)) x = (double)(width - 1);
    return (int)llround(x);
}

int lat_to_y(double lat_deg, int height)
{
    double lat = lat_deg;
    if (lat > 90.0) lat = 90.0;
    if (lat < -90.0) lat = -90.0;
    double y = (90.0 - lat) / 180.0 * (double)(height - 1);
    if (y < 0.0) y = 0.0;
    if (y > (double)(height - 1)) y = (double)(height - 1);
    return (int)llround(y);
}
