#include "rg_assist.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_ASSISTANT

#include <string.h>

void rg_assist_init(RgAssist* a, bool enabled) {
    memset(a, 0, sizeof(*a));
    a->enabled = enabled;
}

void rg_assist_new_context(RgAssist* a) {
    a->context++;
    a->dismissed = 0;
}

RgTip rg_assist_rule(const RgAssistFacts* f) {
    if(f->recording) return RgTipNone;
    if(f->low_memory && !f->classic) return RgTipTryClassic;
    if(f->have_ctx) {
        if(f->ctx_analyzed && f->ctx_too_few_frames) return RgTipRecordMore;
        if(f->ctx_raw && !f->ctx_analyzed) return RgTipAnalyze;
        if(f->ctx_searched && f->ctx_similar > 0 && !f->ctx_compared) return RgTipCompare;
        if(f->ctx_analyzed && !f->ctx_searched) return RgTipFindSimilar;
        if(f->ctx_raw && f->ctx_analyzed && !f->ctx_studio) return RgTipInspectPulses;
        if(f->session_active && !f->ctx_in_session) return RgTipAddToSession;
    }
    if(f->session_active && f->session_signals >= 2 && !f->session_reported)
        return RgTipExportReport;
    return RgTipNone;
}

RgTip rg_assist_update(RgAssist* a, const RgAssistFacts* f, uint32_t now_ms) {
    if(!a->enabled) {
        a->current = RgTipNone;
        return RgTipNone;
    }
    RgTip tip = rg_assist_rule(f);
    if(tip == RgTipNone || (a->dismissed & (1u << tip))) {
        a->current = RgTipNone;
        return RgTipNone;
    }
    if(tip == a->current) return tip; // still relevant: stays
    // A new tip: not too soon after the last one, nor a repeat too soon.
    if(a->any_shown && now_ms - a->last_new_ms < RG_ASSIST_MIN_GAP_MS) {
        a->current = RgTipNone;
        return RgTipNone;
    }
    if((a->shown_mask & (1u << tip)) && now_ms - a->last_shown_ms[tip] < RG_ASSIST_REPEAT_MS) {
        a->current = RgTipNone;
        return RgTipNone;
    }
    a->current = (uint8_t)tip;
    a->last_shown_ms[tip] = now_ms;
    a->shown_mask |= 1u << tip;
    a->last_new_ms = now_ms;
    a->any_shown = true;
    return tip;
}

void rg_assist_dismiss(RgAssist* a) {
    if(a->current != RgTipNone) a->dismissed |= 1u << a->current;
    a->current = RgTipNone;
}

static const char* const rg_tip_texts[RgTipCount] = {
    "",
    "Low memory: try Classic mode?",
    "Few frames: record more samples?",
    "Analyze this signal?",
    "Similar signals found. Compare?",
    "Find similar signals?",
    "Inspect pulse timing?",
    "Add to the session?",
    "Export a session report?",
};

static const uint8_t rg_tip_actions[RgTipCount] = {
    RgTipActionNone,
    RgTipActionSettings,
    RgTipActionReceive,
    RgTipActionDiscovery,
    RgTipActionMultiCompare,
    RgTipActionSimilar,
    RgTipActionStudio,
    RgTipActionSessionAdd,
    RgTipActionSessionReport,
};

const char* rg_tip_text(RgTip tip) {
    return (unsigned)tip < RgTipCount ? rg_tip_texts[tip] : "";
}

RgTipAction rg_tip_action(RgTip tip) {
    return (unsigned)tip < RgTipCount ? (RgTipAction)rg_tip_actions[tip] : RgTipActionNone;
}

#endif /* RG_FEATURE_ASSISTANT */
