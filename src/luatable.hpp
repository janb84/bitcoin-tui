#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "guarded.hpp"

enum class ColumnType { String, Number, DateTime, Date, Time, TimeMS };

std::optional<ColumnType> parse_column_type(const std::string& s);

using CellData = std::variant<std::string, int64_t, double>;

// Format a CellData value for display according to its column type.
std::string format_cell(ColumnType type, const CellData& data, int decimals = -1);

struct ColumnDef {
    std::string name;
    std::string header;
    ColumnType  type     = ColumnType::String;
    int         decimals = -1; // -1 = use type default
};

struct CellValue {
    CellData    data;
    std::string color;
    bool        bold = false;
};

struct Row {
    int                    epoch{0};
    std::vector<CellValue> cells;
};

struct RowCompare {
    size_t key_index = 0;
    bool   operator()(const Row& a, const Row& b) const {
        return a.cells[key_index].data < b.cells[key_index].data;
    }
};

struct RowData {
    std::set<Row, RowCompare> rows;
    int                       current_epoch{0};
    CellValue                 header_info;
};

class LuaTable {
  public:
    LuaTable(const std::string& key_column, std::vector<ColumnDef> columns, std::string title = {},
             bool no_header = false);

    void update(const CellData& key, const std::map<std::string, CellValue>& data);
    bool remove(const CellData& key);
    void start_refresh();
    void finish_refresh();
    void set_header_info(CellValue info);

    std::vector<std::string> keys() const;

    const std::vector<ColumnDef>& columns() const { return columns_; }
    const std::string&            title() const { return title_; }
    bool                          no_header() const { return no_header_; }
    size_t                        key_index() const { return key_index_; }

    ColumnType key_type() const { return columns_[key_index_].type; }

    // Thread-safe access to rows
    template <typename F> auto access(F&& f) const {
        return rows_.access([&](const auto& rd) { return f(rd.rows); });
    }

    CellValue header_info() const {
        return rows_.access([](const auto& rd) { return rd.header_info; });
    }

  private:
    const std::vector<ColumnDef> columns_;
    const std::string            title_;
    const bool                   no_header_;
    const size_t                 key_index_;
    Guarded<RowData>             rows_;

    size_t col_index(const std::string& name) const;
};

using LuaTableVec = std::vector<std::shared_ptr<LuaTable>>;
