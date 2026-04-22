
-- Requires --allow-rpc=getwalletinfo,getbalances,listunspent,listtransactions

local WALLET_NAME = btcui_option("wallet", "")
if WALLET_NAME == "" then
  btcui_set_name("Default Wallet")
else
  btcui_set_name(WALLET_NAME .. " Wallet")
end
local REFRESH_INTERVAL = 5

local wallet_info_panel = btcui_summary({
    title = "Wallet Info",
    fields = {
        { name = "walletname",      label = "Wallet" },
        { name = "format",          label = "Format" },
        { name = "txcount",         label = "Transactions",    type = "number" },
        { name = "keypoolsize",     label = "Keypool",         type = "number" },
        { name = "keypoolsize_int", label = "Keypool (int)",   type = "number" },
        { name = "descriptors",     label = "Descriptors" },
        { name = "private_keys",    label = "Private keys" },
        { name = "external_signer", label = "External signer" },
        { name = "avoid_reuse",     label = "Avoid reuse" },
        { name = "scanning",        label = "Scanning" },
        { name = "birthtime",       label = "Birth time",      type = "datetime" },
    },
})

local balance_panel = btcui_summary({
    title = "Balances",
    fields = {
        { name = "trusted",   label = "Trusted",   type = "number", decimals = 8 },
        { name = "pending",   label = "Pending",   type = "number", decimals = 8 },
        { name = "immature",  label = "Immature",  type = "number", decimals = 8 },
        { name = "used",      label = "Used",      type = "number", decimals = 8 },
    },
})

local unspent_table = btcui_table({
    key = "outpoint",
    title = "Unspent Outputs (UTXOs)",
    columns = {
        { name = "outpoint",      header = "OutPoint" },
        { name = "amount",        header = "Amount (BTC)", type = "number", decimals = 8 },
        { name = "confirmations", header = "Confirms",     type = "number" },
        { name = "address",       header = "Address" },
    },
})

local transactions_table = btcui_table({
    key = "txid",
    title = "Recent Transactions",
    columns = {
        { name = "txid",          header = "TXID" },
        { name = "time",          header = "Time",         type = "timestamp" },
        { name = "category",      header = "Category" },
        { name = "amount",        header = "Amount (BTC)", type = "number", decimals = 8 },
        { name = "confirmations", header = "Confirms",     type = "number" },
        { name = "address",       header = "Address" },
    },
})

local function format_btc(amount)
    return tonumber(amount) or 0
end

local function yesno(v)
    if v == nil then return "N/A" end
    return v and "yes" or "no"
end

local function walletname(wn)
    if wn ~= "" then return { value = wn, bold = true, color = "yellow" } end
    return "default (unnamed)"
end

btcui_set_interval(REFRESH_INTERVAL, function()
    local wi = btcui_rpc_wallet(WALLET_NAME, "getwalletinfo")
    if wi then
        local scanning = "no"
        if type(wi.scanning) == "table" then
            scanning = string.format("%.1f%%", (wi.scanning.progress or 0) * 100)
        end
        wallet_info_panel:set({
            walletname      = walletname(wi.walletname),
            format          = wi.format or "N/A",
            txcount         = wi.txcount or 0,
            keypoolsize     = wi.keypoolsize or 0,
            keypoolsize_int = wi.keypoolsize_hd_internal or 0,
            descriptors     = { value = yesno(wi.descriptors),
                                color = wi.descriptors and "green" or "yellow" },
            private_keys    = { value = yesno(wi.private_keys_enabled),
                                color = wi.private_keys_enabled and "yellow" or "green" },
            external_signer = yesno(wi.external_signer),
            avoid_reuse     = yesno(wi.avoid_reuse),
            scanning        = scanning,
            birthtime       = wi.birthtime or 0,
        })
    end

    local bal = btcui_rpc_wallet(WALLET_NAME, "getbalances")
    if bal and bal.mine then
        balance_panel:set({
            trusted  = format_btc(bal.mine.trusted),
            pending  = format_btc(bal.mine.untrusted_pending),
            immature = format_btc(bal.mine.immature),
            used     = format_btc(bal.mine.used or 0),
        })
    end

    local unspent = btcui_rpc_wallet(WALLET_NAME, "listunspent")
    if unspent then
        unspent_table:start_refresh()
        for _, utxo in ipairs(unspent) do
            unspent_table:update(utxo.txid .. ":" .. utxo.vout, {
                amount        = format_btc(utxo.amount),
                confirmations = utxo.confirmations,
                address       = btcui_address(utxo.address),
            })
        end
        unspent_table:finish_refresh()
    end

    local transactions = btcui_rpc_wallet(WALLET_NAME, "listtransactions", "*", 10, 0)
    if transactions then
        transactions_table:start_refresh()
        for _, tx in ipairs(transactions) do
            transactions_table:update(tx.txid, {
                txid          = tx.txid,
                time          = tx.time,
                category      = tx.category,
                amount        = format_btc(tx.amount),
                confirmations = tx.confirmations,
                address       = btcui_address(tx.address),
            })
        end
        transactions_table:finish_refresh()
    end
end)
