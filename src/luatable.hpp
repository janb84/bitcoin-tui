#pragma once

#include <compare>
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

struct Address {
    std::string value;
    auto        operator<=>(const Address&) const = default;
};
// A progress-bar cell. frac is in [0, 1]; prefix is optional bold text shown
// before the bar (e.g. "1.2 MB / 300 MB"). Rendered via gauge_element().
struct Gauge {
    double      frac = 0.0;
    std::string prefix;
    auto        operator<=>(const Gauge&) const = default;
};
using CellData = std::variant<std::string, int64_t, double, Address, Gauge>;

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
    bool                   selectable{true}; // false → display-only (navigation skips it)
    std::vector<CellValue> cells;
};

struct RowCompare {
    using is_transparent = void;
    size_t key_index     = 0;
    bool   operator()(const Row& a, const Row& b) const {
        return a.cells[key_index].data < b.cells[key_index].data;
    }
    bool operator()(const Row& a, const CellData& b) const { return a.cells[key_index].data < b; }
    bool operator()(const CellData& a, const Row& b) const { return a < b.cells[key_index].data; }
};

struct RowData {
    std::set<Row, RowCompare> rows;
    int                       current_epoch{0};
    CellValue                 header_info;
};

class LuaPanel {
  public:
    virtual ~LuaPanel()                      = default;
    virtual const std::string& title() const = 0;
};

class LuaTable : public LuaPanel {
  public:
    LuaTable(const std::string& key_column, std::vector<ColumnDef> columns, std::string title = {},
             bool no_header = false, bool selectable = true);

    void update(const CellData& key, const std::map<std::string, CellValue>& data,
                bool selectable = true);
    bool remove(const CellData& key);
    void start_refresh();
    void finish_refresh();
    void set_header_info(CellValue info);

    std::vector<std::string> keys() const;

    const std::vector<ColumnDef>& columns() const { return columns_; }
    const std::string&            title() const override { return title_; }
    bool                          no_header() const { return no_header_; }
    // When false, the table is display-only: keyboard/mouse navigation skips it so
    // its rows can't be focused, selected, or activated (no btcui_on_select).
    bool   selectable() const { return selectable_; }
    size_t key_index() const { return key_index_; }

    // Per-row selectability (a row may opt out via __selectable=false in update()).
    // Indices are into the current row iteration order (key order).
    bool any_selectable() const;           // true if at least one row is selectable
    bool is_row_selectable(int idx) const; // false for out-of-range or display-only rows
    int  first_selectable_row() const;     // first selectable index, or -1
    // Next selectable index from `from` moving by `dir` (+1/-1); -1 if none in range.
    int next_selectable_row(int from, int dir) const;

    ColumnType key_type() const { return columns_[key_index_].type; }

    // Thread-safe access to rows
    template <typename F> auto access(F&& f) const {
        return rows_.access([&](const auto& rd) { return f(rd.rows); });
    }

    CellValue header_info() const {
        return rows_.access([](const auto& rd) { return rd.header_info; });
    }

    // Row selection (UI thread writes, Lua thread reads via selected_key())
    std::atomic<int>& selected_row() { return selected_row_; }
    // Returns the key of the selected row as a string, or nullopt if nothing is selected.
    std::optional<std::string> selected_key() const;
    // Returns the formatted value of a named column in the selected row, or nullopt.
    std::optional<std::string> selected_value(const std::string& column_name) const;

  private:
    const std::vector<ColumnDef> columns_;
    const std::string            title_;
    const bool                   no_header_;
    const bool                   selectable_;
    const size_t                 key_index_;
    Guarded<RowData>             rows_;
    std::atomic<int>             selected_row_{-1};

    size_t col_index(const std::string& name) const;
};

class LuaSummary : public LuaPanel {
  public:
    LuaSummary(std::vector<ColumnDef> fields, std::string title = {}, bool new_row = false);

    void set(const std::map<std::string, CellValue>& values);

    const std::vector<ColumnDef>& fields() const { return fields_; }
    const std::string&            title() const override { return title_; }
    // When true, this summary starts a fresh side-by-side row instead of
    // joining the run of preceding summaries.
    bool new_row() const { return new_row_; }

    template <typename F> auto access(F&& f) const {
        return values_.access([&](const auto& v) { return f(v); });
    }

  private:
    const std::vector<ColumnDef>    fields_;
    const std::string               title_;
    const bool                      new_row_;
    Guarded<std::vector<CellValue>> values_;

    size_t field_index(const std::string& name) const;
};
