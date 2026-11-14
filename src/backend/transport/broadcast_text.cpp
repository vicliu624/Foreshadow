#include "signalscope/backend/transport/broadcast_text.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "iso6937.hpp"
#include <array>
#include <algorithm>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <iconv.h>
#endif

namespace signalscope::transport {
bool supportedBroadcastEncoding(const std::string& name) {
    for(const auto* value:{"ISO_6937","GB18030","GB2312","UTF-8"})if(name==value)return true;
    return false;
}
nlohmann::json broadcastTextParameter() {
    nlohmann::json field={{"key","text_encoding"},{"type","enum"},{"extension",true},{"default","ISO_6937"},
        {"choices",nlohmann::json::array({"GB18030","ISO_6937","GB2312","UTF-8"})},
        {"label_key","text.encoding"},{"description_key","text.encoding_hint"}};
    static const auto catalog=TextCatalog::resource("broadcast-text");catalog.resolve(field);return field;
}
namespace {
std::optional<std::string> nativeConvert(std::span<const std::uint8_t> bytes,const std::string& encoding) {
    if(bytes.empty())return std::string{};
#ifdef _WIN32
    unsigned codepage=0;
    if(encoding=="UTF-8")codepage=CP_UTF8;
    else if(encoding=="GB2312")codepage=936;
    else if(encoding=="GB18030")codepage=54936;
    else if(encoding=="EUC-KR")codepage=51949;
    else if(encoding=="BIG5")codepage=950;
    else if(encoding.starts_with("ISO-8859-"))codepage=28590+static_cast<unsigned>(std::stoi(encoding.substr(9)));
    std::wstring wide;
    if(encoding=="UTF-16BE") {
        if(bytes.size()%2)return std::nullopt;
        for(std::size_t i=0;i<bytes.size();i+=2)wide.push_back(wchar_t((unsigned(bytes[i])<<8)|bytes[i+1]));
    }else {
        if(!codepage || !IsValidCodePage(codepage))return std::nullopt;
        const auto size=MultiByteToWideChar(codepage,MB_ERR_INVALID_CHARS,reinterpret_cast<const char*>(bytes.data()),static_cast<int>(bytes.size()),nullptr,0);
        if(size<=0)return std::nullopt;wide.resize(size);
        if(MultiByteToWideChar(codepage,MB_ERR_INVALID_CHARS,reinterpret_cast<const char*>(bytes.data()),static_cast<int>(bytes.size()),wide.data(),size)!=size)return std::nullopt;
    }
    const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),static_cast<int>(wide.size()),nullptr,0,nullptr,nullptr);
    if(size<=0)return std::nullopt;std::string output(size,'\0');
    if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),static_cast<int>(wide.size()),output.data(),size,nullptr,nullptr)!=size)return std::nullopt;
    return output;
#else
    const auto codec=iconv_open("UTF-8",encoding.c_str());if(codec==reinterpret_cast<iconv_t>(-1))return std::nullopt;
    std::string result(bytes.size()*4+16,'\0');char* output=result.data();std::size_t available=result.size(),remaining=bytes.size();
    auto* input=reinterpret_cast<char*>(const_cast<std::uint8_t*>(bytes.data()));
    const auto status=iconv(codec,&input,&remaining,&output,&available);iconv_close(codec);
    if(status==static_cast<std::size_t>(-1) || remaining)return std::nullopt;
    result.resize(result.size()-available);return result;
#endif
}
std::optional<std::string> convert(std::span<const std::uint8_t> bytes,const std::string& encoding) {
    if(encoding!="ISO_6937")return nativeConvert(bytes,encoding);
    std::string output;
    for(std::size_t i=0;i<bytes.size();++i) {
        const auto byte=bytes[i];auto unicode=iso6937::single[byte];
        if(byte>=0xc1 && byte<=0xcf) {
            if(++i>=bytes.size())return std::nullopt;
            const auto pair=std::uint16_t((std::uint16_t(byte)<<8)|bytes[i]);
            const auto entry=std::lower_bound(iso6937::combined.begin(),iso6937::combined.end(),pair,
                [](const auto& value,std::uint16_t key){return value.pair<key;});
            if(entry==iso6937::combined.end() || entry->pair!=pair)return std::nullopt;
            unicode=entry->unicode;
        }
        if(unicode==iso6937::invalid)return std::nullopt;
        if(unicode<0x80)output+=static_cast<char>(unicode);
        else if(unicode<0x800) {
            output+=static_cast<char>(0xc0|(unicode>>6));output+=static_cast<char>(0x80|(unicode&0x3f));
        }else {
            output+=static_cast<char>(0xe0|(unicode>>12));output+=static_cast<char>(0x80|((unicode>>6)&0x3f));output+=static_cast<char>(0x80|(unicode&0x3f));
        }
    }
    return output;
}
}
std::optional<std::string> decodeBroadcastText(std::span<const std::uint8_t> input,const std::string& unmarked) {
    if(input.size()>16384)return std::nullopt;
    if(input.empty())return std::string{};
    std::string encoding=unmarked;const auto selector=input[0];
    if(selector>=1 && selector<=11) {
        const auto part=selector+4;if(part==12)return std::nullopt;encoding="ISO-8859-"+std::to_string(part);input=input.subspan(1);
    }else if(selector==0x10) {
        if(input.size()<3 || input[1] || !input[2] || input[2]>16 || input[2]==12)return std::nullopt;
        encoding="ISO-8859-"+std::to_string(input[2]);input=input.subspan(3);
    }else if(selector==0x11){encoding="UTF-16BE";input=input.subspan(1);}
    else if(selector==0x12){encoding="EUC-KR";input=input.subspan(1);}
    else if(selector==0x13){encoding="GB2312";input=input.subspan(1);}
    else if(selector==0x14){encoding="BIG5";input=input.subspan(1);}
    else if(selector==0x15){encoding="UTF-8";input=input.subspan(1);}
    else if(selector<0x20)return std::nullopt;
    try{return convert(input,encoding);}catch(const std::exception&){return std::nullopt;}
}
}
