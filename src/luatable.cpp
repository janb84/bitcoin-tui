#include "luatable.hpp"

#include "format.hpp"

#include <cmath>
#include <map>

std::optional<ColumnType> parse_column_type(const std::string& s) {
    if (s.empty() || s == "string")
        return ColumnType::String;
    if (s == "number")
        return ColumnType::Number;
    if (s == "datetime")
        return ColumnType::DateTime;
    if (s == "date")
        return ColumnType::Date;
    if (s == "time")
        return ColumnType::Time;
    if (s == "time_ms" || s == "timestamp")
        return ColumnType::TimeMS;
    return std::nullopt;
}

std::string format_cell(ColumnType type, const CellData& data, int decimals) {
    if (std::holds_alternative<Address>(data))
        return std::get<Address>(data).value;
    // Unset cells (default string) render as blank for numeric types
    if (std::holds_alternative<std::string>(data) && std::get<std::string>(data).empty() &&
        type != ColumnType::String) {
        return {};
    }
    char buf[64];
    switch (type) {
    case ColumnType::DateTime:
    case ColumnType::Date:
    case ColumnType::Time:
    case ColumnType::TimeMS: {
        double value = std::holds_alternative<double>(data) ? std::get<double>(data) : 0.0;
        static constexpr TimeFmt fmts[] = {TimeFmt::YMDHMS, TimeFmt::YMD, TimeFmt::HMS,
                                           TimeFmt::HMSM};
        return fmt_localtime(to_time_point(value),
                             fmts[static_cast<int>(type) - static_cast<int>(ColumnType::DateTime)]);
    }
    case ColumnType::Number: {
        if (decimals >= 0) {
            double value = std::holds_alternative<double>(data) ? std::get<double>(data)
                           : std::holds_alternative<int64_t>(data)
                               ? static_cast<double>(std::get<int64_t>(data))
                               : 0.0;
            snprintf(buf, sizeof(buf), "%.*f", decimals, value);
            return buf;
        }
        if (std::holds_alternative<int64_t>(data)) {
            return std::to_string(std::get<int64_t>(data));
        }
        double value = std::holds_alternative<double>(data) ? std::get<double>(data) : 0.0;
        if (value == std::floor(value)) {
            snprintf(buf, sizeof(buf), "%.0f", value);
        } else {
            snprintf(buf, sizeof(buf), "%g", value);
        }
        return buf;
    }
    case ColumnType::String:
        if (std::holds_alternative<std::string>(data))
            return std::get<std::string>(data);
        if (std::holds_alternative<int64_t>(data))
            return std::to_string(std::get<int64_t>(data));
        if (std::holds_alternative<double>(data)) {
            snprintf(buf, sizeof(buf), "%g", std::get<double>(data));
            return buf;
        }
        return {};
    }
    return {};
}

static std::vector<ColumnDef> ensure_key_column(std::vector<ColumnDef> columns,
                                                const std::string&     key_column) {
    for (const auto& c : columns) {
        if (c.name == key_column)
            return columns;
    }
    columns.insert(columns.begin(), {key_column, "", ColumnType::Number});
    return columns;
}

LuaTable::LuaTable(const std::string& key_column, std::vector<ColumnDef> columns, std::string title,
                   bool no_header)
    : columns_(ensure_key_column(std::move(columns), key_column)), title_(std::move(title)),
      no_header_(no_header), key_index_(col_index(key_column)),
      rows_(RowData{std::set<Row, RowCompare>(RowCompare{key_index_}), 0}) {}

size_t LuaTable::col_index(const std::string& name) const {
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].name == name)
            return i;
    }
    return columns_.size();
}

void LuaTable::update(const CellData& key, const std::map<std::string, CellValue>& data) {
    Row row;
    row.cells.resize(columns_.size());
    row.cells[key_index_].data = key;

    for (const auto& [name, cell] : data) {
        size_t idx = col_index(name);
        if (idx < columns_.size() && idx != key_index_) {
            row.cells[idx] = cell;
        }
    }

    rows_.update([&](auto& rd) {
        auto it = rd.rows.find(key);
        if (it != rd.rows.end())
            rd.rows.erase(it);
        row.epoch = rd.current_epoch;
        rd.rows.insert(std::move(row));
    });
}

bool LuaTable::remove(const CellData& key) {
    return rows_.update([&](auto& rd) {
        auto it = rd.rows.find(key);
        if (it == rd.rows.end())
            return false;
        rd.rows.erase(it);
        return true;
    });
}

void LuaTable::start_refresh() {
    rows_.update([](auto& rd) { ++rd.current_epoch; });
}

void LuaTable::finish_refresh() {
    rows_.update([](auto& rd) {
        std::erase_if(rd.rows, [&](const auto& row) { return row.epoch != rd.current_epoch; });
    });
}

void LuaTable::set_header_info(CellValue info) {
    rows_.update([&](auto& rd) { rd.header_info = std::move(info); });
}

// --- LuaSummary ---

LuaSummary::LuaSummary(std::vector<ColumnDef> fields, std::string title)
    : fields_(std::move(fields)), title_(std::move(title)),
      values_(std::vector<CellValue>(fields_.size())) {}

size_t LuaSummary::field_index(const std::string& name) const {
    for (size_t i = 0; i < fields_.size(); ++i) {
        if (fields_[i].name == name)
            return i;
    }
    return fields_.size();
}

void LuaSummary::set(const std::map<std::string, CellValue>& values) {
    values_.update([&](auto& v) {
        for (const auto& [name, cell] : values) {
            size_t idx = field_index(name);
            if (idx < fields_.size())
                v[idx] = cell;
        }
    });
}

// --- LuaTable ---

std::vector<std::string> LuaTable::keys() const {
    return rows_.access([&](const auto& rd) {
        std::vector<std::string> result;
        result.reserve(rd.rows.size());
        for (const auto& row : rd.rows) {
            result.push_back(format_cell(columns_[key_index_].type, row.cells[key_index_].data));
        }
        return result;
    });
}

std::optional<std::string> LuaTable::selected_key() const {
    int idx = selected_row_.load();
    return rows_.access([&](const auto& rd) -> std::optional<std::string> {
        if (idx < 0 || idx >= static_cast<int>(rd.rows.size()))
            return std::nullopt;
        auto it = rd.rows.begin();
        std::advance(it, idx);
        return format_cell(columns_[key_index_].type, it->cells[key_index_].data);
    });
}

std::optional<std::string> LuaTable::selected_value(const std::string& column_name) const {
    int idx = selected_row_.load();
    // Find column index by name
    size_t col_idx = columns_.size();
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (columns_[i].name == column_name) {
            col_idx = i;
            break;
        }
    }
    if (col_idx == columns_.size())
        return std::nullopt;
    return rows_.access([&](const auto& rd) -> std::optional<std::string> {
        if (idx < 0 || idx >= static_cast<int>(rd.rows.size()))
            return std::nullopt;
        auto it = rd.rows.begin();
        std::advance(it, idx);
        if (col_idx >= it->cells.size())
            return std::nullopt;
        // For Address cells, return the raw address string
        const auto& data = it->cells[col_idx].data;
        if (const auto* addr = std::get_if<Address>(&data))
            return addr->value;
        return format_cell(columns_[col_idx].type, data);
    });
}
