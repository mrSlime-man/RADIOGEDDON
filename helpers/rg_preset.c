#include "rg_preset.h"

RgPresetResult rg_preset_check(const uint8_t* data, size_t size, uint8_t* bad) {
    if(!data || size == 0) return RgPresetEmpty;
    // Walk the pairs as the firmware does: a 00 register ends them, whatever
    // byte follows it, and the PA table starts two bytes later.
    for(size_t i = 0; i < size; i += 2) {
        uint8_t reg = data[i];
        if(reg == 0) {
            if(size - i < 2u + RG_PRESET_PA_TABLE_SIZE) return RgPresetShortPaTable;
            return RgPresetOk;
        }
        if(reg > RG_PRESET_LAST_CONFIG_REG) {
            if(bad) *bad = reg;
            return RgPresetBadRegister;
        }
        if(i + 1 >= size) break; // a register with no value: the list never ends
    }
    return RgPresetNoEnd;
}

const char* rg_preset_result_text(RgPresetResult result) {
    switch(result) {
    case RgPresetOk:
        return "OK";
    case RgPresetEmpty:
        return "No preset data";
    case RgPresetNoEnd:
        return "Preset has no end";
    case RgPresetShortPaTable:
        return "Preset PA table cut";
    case RgPresetBadRegister:
        return "Preset writes a command";
    }
    return "Bad preset";
}
