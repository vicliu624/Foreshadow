#include "plugins/desktop/research/research_range.hpp"
#include <QStringList>
#include <algorithm>
#include <limits>

namespace signalscope::desktop {
namespace {
std::optional<std::uint64_t> secondsToSamples(const QString& value,std::uint32_t rate) {
    if(!rate || rate>100'000'000)return {};
    const auto parts=value.split(u'.');
    if(parts.size()>2 || parts.front().isEmpty())return {};
    for(const auto& part:parts)
        for(const auto digit:part)
            if(digit<u'0' || digit>u'9')return {};
    bool valid=false;
    const auto whole=parts.front().toULongLong(&valid);
    if(!valid)return {};
    constexpr std::uint64_t scale=1'000'000'000;
    std::uint64_t fractional=0;
    if(parts.size()==2) {
        const auto& digits=parts.back();
        if(digits.isEmpty() || digits.size()>9)return {};
        fractional=digits.leftJustified(9,u'0').toULongLong();
    }
    const auto rounded=(fractional*rate+scale/2)/scale;
    if(whole>(std::numeric_limits<std::uint64_t>::max()-rounded)/rate)return {};
    return whole*rate+rounded;
}
}

std::optional<ResearchSampleRange> researchRangeFromSeconds(const QString& start,const QString& duration,
                                                           std::uint32_t rate) {
    const auto first=secondsToSamples(start,rate);
    const auto count=secondsToSamples(duration,rate);
    if(!first || !count || !*count)return {};
    return ResearchSampleRange{*first,*count};
}

std::optional<ResearchSampleRange> replayEventRange(const Json& event,std::uint32_t rate,std::uint64_t pairs) {
    if(!event.is_object() || !rate)return {};
    const auto sample=[&event](const char* key)->std::optional<std::uint64_t>{
        const auto value=event.find(key);
        if(value==event.end())return {};
        if(value->is_number_unsigned())return value->get<std::uint64_t>();
        if(value->is_number_integer() && value->get<std::int64_t>()>=0)return value->get<std::uint64_t>();
        return {};
    };
    const auto observed=sample("first_observed_sample");
    if(!observed || *observed>=pairs)return {};
    const auto margin=std::max<std::uint64_t>(1,rate/50);
    auto end=std::max(*observed+1,sample("end_sample_exclusive").value_or(0));
    if(const auto last=sample("last_observed_sample"))
        end=std::max(end,*last<pairs?*last+1:pairs);
    end=std::min(pairs,std::max(end,sample("confirmed_at_sample").value_or(0)));
    const auto first=*observed-std::min(*observed,margin);
    const auto count=std::min<std::uint64_t>(end-first+std::min(pairs-end,margin),2'000'000);
    if(count<16)return {};
    return ResearchSampleRange{first,count};
}
}
