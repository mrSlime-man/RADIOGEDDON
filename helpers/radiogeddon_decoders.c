#include "radiogeddon_decoders.h"

#include <string.h>

bool radiogeddon_decoders_fed(const SubGhzProtocol* protocol, SubGhzProtocolFlag filter) {
    return protocol && protocol->decoder && protocol->decoder->alloc &&
           (protocol->flag & filter) != 0;
}

size_t radiogeddon_decoders_select(
    const SubGhzProtocolRegistry* all,
    SubGhzProtocolFlag filter,
    const SubGhzProtocol** out,
    size_t cap) {
    size_t n = 0;
    size_t count = subghz_protocol_registry_count(all);
    for(size_t i = 0; i < count; i++) {
        const SubGhzProtocol* p = subghz_protocol_registry_get_by_index(all, i);
        if(!radiogeddon_decoders_fed(p, filter)) continue;
        if(n < cap) out[n] = p;
        n++;
    }
    return n;
}

bool radiogeddon_decoders_skips_binraw(const SubGhzProtocolRegistry* all, SubGhzProtocolFlag filter) {
    size_t count = subghz_protocol_registry_count(all);
    for(size_t i = 0; i < count; i++) {
        const SubGhzProtocol* p = subghz_protocol_registry_get_by_index(all, i);
        if(p && p->decoder && p->decoder->alloc && (p->flag & SubGhzProtocolFlag_BinRAW) &&
           !radiogeddon_decoders_fed(p, filter))
            return true;
    }
    return false;
}

SubGhzReceiver* radiogeddon_decoders_receiver_alloc(
    SubGhzEnvironment* environment,
    const SubGhzProtocolRegistry* all,
    SubGhzProtocolFlag filter) {
    // Every protocol fits: the list is at most the registry's size. It is
    // only read while the receiver allocates its decoders.
    size_t count = subghz_protocol_registry_count(all);
    const SubGhzProtocol** items = malloc((count ? count : 1) * sizeof(*items));
    size_t n = radiogeddon_decoders_select(all, filter, items, count);
    const SubGhzProtocolRegistry fed = {.items = items, .size = n};

    subghz_environment_set_protocol_registry(environment, (void*)&fed);
    SubGhzReceiver* receiver = subghz_receiver_alloc_init(environment);
    subghz_environment_set_protocol_registry(environment, (void*)all);
    free(items);

    subghz_receiver_set_filter(receiver, filter);
    return receiver;
}
