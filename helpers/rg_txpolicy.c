#include "rg_txpolicy.h"

#include <stdio.h>

RgTxVerdict rg_txpolicy_check(const RgTxFacts* facts, bool region_gate) {
    if(!facts->device_valid) return RgTxDeniedDevice;
    if(!region_gate) return RgTxAllowed;
    if(!facts->hardware_valid) return RgTxDeniedHardware;
    if(!facts->region_provisioned) return RgTxDeniedNoRegion;
    if(!facts->region_allows) return RgTxDeniedRegion;
    return RgTxAllowed;
}

const char* rg_txpolicy_short(RgTxVerdict verdict) {
    switch(verdict) {
    case RgTxAllowed:
        return "";
    case RgTxDeniedDevice:
        return "Radio cannot tune";
    case RgTxDeniedHardware:
        return "Outside TX bands";
    case RgTxDeniedNoRegion:
        return "Region unknown";
    case RgTxDeniedRegion:
        return "Region forbids";
    }
    return "TX refused";
}

void rg_txpolicy_explain(
    RgTxVerdict verdict,
    const char* region,
    const char* freq_text,
    char* out,
    size_t out_size) {
    if(!out || out_size == 0) return;
    if(!freq_text) freq_text = "?";
    if(!region || !region[0]) region = "--";
    switch(verdict) {
    case RgTxAllowed:
        snprintf(out, out_size, "TX allowed on\n%s MHz.", freq_text);
        break;
    case RgTxDeniedDevice:
        snprintf(out, out_size, "The radio in use\ncannot tune\n%s MHz.", freq_text);
        break;
    case RgTxDeniedHardware:
        snprintf(out, out_size, "%s MHz is outside\nthe radio's\ntransmit bands.", freq_text);
        break;
    case RgTxDeniedNoRegion:
        snprintf(
            out,
            out_size,
            "Firmware has no\nregion info, so TX\nis off. Update the\nfirmware online.");
        break;
    case RgTxDeniedRegion:
        snprintf(out, out_size, "Region %s does not\nallow TX on\n%s MHz.", region, freq_text);
        break;
    default:
        snprintf(out, out_size, "TX refused.");
        break;
    }
}
