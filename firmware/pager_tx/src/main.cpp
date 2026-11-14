#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>

// Pager SX1262：引脚与电源初始化独立于 Trail Mate 业务实现。
constexpr int RADIO_CS=36,RADIO_IRQ=14,RADIO_RESET=47,RADIO_BUSY=48;
SX1262 radio=new Module(RADIO_CS,RADIO_IRQ,RADIO_RESET,RADIO_BUSY);
struct Config {
    uint32_t frequency=478875000,bandwidth=250000;
    uint8_t sf=11,cr=5,sync=0x12;
    int8_t power=-9;
    uint16_t preamble=8;
    bool inverted=false,gfsk=false;
} config;
bool ready=false,transmitting=false,expander_ready=false;
volatile bool tx_done=false;
volatile uint32_t tx_irq_us=0;
uint8_t payload[240];
char command[160];size_t command_length=0;bool overflow=false;
uint32_t tx_id=0,tx_length=0,tx_start_us=0,tx_airtime_us=0,last_tx_ms=0,cooldown_ms=0;
uint32_t started_count=0,completed_count=0,completed_id=0,completed_elapsed=0,payload_hash=0;
int last_result=0;

void IRAM_ATTR interrupt_tx_done(){tx_irq_us=micros();tx_done=true;}
void error(const char* message,int code=0){Serial.printf("{\"ok\":false,\"error\":\"%s\",\"code\":%d}\n",message,code);}
bool read_register(uint8_t reg,uint8_t& result){
    Wire.beginTransmission(0x20);Wire.write(reg);
    if(Wire.endTransmission(false)!=0||Wire.requestFrom(uint8_t(0x20),uint8_t(1))!=1)return false;
    result=Wire.read();return true;
}
bool write_register(uint8_t reg,uint8_t value){Wire.beginTransmission(0x20);Wire.write(reg);Wire.write(value);return Wire.endTransmission()==0;}
bool enable_radio_power(){
    uint8_t output=0,direction=0;
    if(!read_register(2,output)||!read_register(6,direction))return false;
    // XL9555 port0 bit3：只修改 LoRa 电源位，不打开其他外设。
    if(!write_register(2,output|8)||!write_register(6,direction&uint8_t(~8)))return false;
    delay(50);
    return read_register(2,output)&&read_register(6,direction)&&(output&8)&&!(direction&8);
}
int configure(const Config& value){
    int status=value.gfsk
        ? radio.beginFSK(value.frequency/1e6f,2.4f,4.8f,156.2f,value.power,32,1.6f)
        : radio.begin(value.frequency/1e6f,value.bandwidth/1000.0f,value.sf,value.cr,value.sync,value.power,value.preamble,1.6f);
    if(status==RADIOLIB_ERR_NONE)status=radio.setDio2AsRfSwitch(true);
    if(status==RADIOLIB_ERR_NONE)status=radio.setCRC(2);
    if(status==RADIOLIB_ERR_NONE && value.gfsk)status=radio.setDataShaping(RADIOLIB_SHAPING_0_5);
    if(status==RADIOLIB_ERR_NONE && !value.gfsk)status=radio.explicitHeader();
    if(status==RADIOLIB_ERR_NONE && !value.gfsk)status=radio.invertIQ(value.inverted);
    if(status==RADIOLIB_ERR_NONE){radio.setDio1Action(interrupt_tx_done);status=radio.standby();}
    return status;
}
void status(){
    Serial.printf("{\"ok\":true,\"kind\":\"mode\",\"modulation\":\"%s\"}\n",config.gfsk?"gfsk":"lora");
    Serial.printf("{\"ok\":true,\"kind\":\"status\",\"firmware\":\"amc-pager-tx-0.3\",\"radio_ready\":%s,\"expander_ready\":%s,\"transmitting\":%s,\"frequency_hz\":%lu,\"bandwidth_hz\":%lu,\"sf\":%u,\"cr\":%u,\"power_dbm\":%d,\"preamble\":%u,\"sync\":%u,\"inverted\":%s,\"uptime_ms\":%lu,\"last_started_id\":%lu,\"started_count\":%lu,\"completed_count\":%lu,\"last_completed_id\":%lu,\"last_elapsed_us\":%lu,\"last_result\":%d,\"last_length\":%lu,\"last_airtime_us\":%lu,\"last_payload_hash\":%lu,\"last_start_us\":%lu}\n",
        ready?"true":"false",expander_ready?"true":"false",transmitting?"true":"false",
        static_cast<unsigned long>(config.frequency),static_cast<unsigned long>(config.bandwidth),config.sf,config.cr,config.power,config.preamble,config.sync,config.inverted?"true":"false",static_cast<unsigned long>(millis()),static_cast<unsigned long>(tx_id),static_cast<unsigned long>(started_count),static_cast<unsigned long>(completed_count),static_cast<unsigned long>(completed_id),static_cast<unsigned long>(completed_elapsed),last_result,static_cast<unsigned long>(tx_length),static_cast<unsigned long>(tx_airtime_us),static_cast<unsigned long>(payload_hash),static_cast<unsigned long>(tx_start_us));
}
bool unsigned_value(const char* text,uint32_t maximum,uint32_t& value){
    if(!text||!*text||*text=='-')return false;
    char* end=nullptr;errno=0;const auto parsed=strtoul(text,&end,0);
    if(errno||!end||*end||parsed>maximum)return false;value=parsed;return true;
}
void dispatch(char* line){
    char* words[12];size_t count=0;char* context=nullptr;
    for(char* word=strtok_r(line," \t",&context);word;word=strtok_r(nullptr," \t",&context)){
        if(count==12){error("too_many_arguments");return;}words[count++]=word;
    }
    if(!count)return;
    if(count==1&&(!strcmp(words[0],"PING")||!strcmp(words[0],"STATUS"))){status();return;}
    if(count==1&&!strcmp(words[0],"STOP")){
        if(transmitting){radio.standby();radio.finishTransmit();transmitting=false;tx_done=false;last_result=RADIOLIB_ERR_TX_TIMEOUT;Serial.printf("{\"ok\":true,\"kind\":\"aborted\",\"id\":%lu}\n",static_cast<unsigned long>(tx_id));}
        else Serial.println("{\"ok\":true,\"kind\":\"idle\"}");return;
    }
    if(transmitting){error("busy");return;}
    if(count==3&&!strcmp(words[0],"AMP")){
        uint32_t frequency;
        const bool ask=!strcmp(words[2],"ASK");
        if((!ask&&strcmp(words[2],"OOK"))||!unsigned_value(words[1],510000000,frequency)||frequency<470000000){
            error("invalid_configuration");return;
        }
        if(!ready){error("radio_not_ready");return;}
        if(uint32_t(millis()-last_tx_ms)<cooldown_ms){error("cooldown");return;}
        Config probe=config;probe.frequency=frequency;probe.power=-9;
        int result=configure(probe);
        last_tx_ms=millis();cooldown_ms=2000;
        // Twelve fixed-duration on/low pairs; no continuous-wave serial command.
        for(unsigned i=0;i<24 && result==RADIOLIB_ERR_NONE;++i) {
            result=radio.standby();
            if(result==RADIOLIB_ERR_NONE && (ask || i%2==0)) {
                result=radio.setOutputPower(ask && i%2==0?-3:-9);
                if(result==RADIOLIB_ERR_NONE)result=radio.transmitDirect();
            }
            if(result==RADIOLIB_ERR_NONE)delayMicroseconds(2500);
        }
        const auto stopped=radio.standby();
        ready=configure(config)==RADIOLIB_ERR_NONE;
        if(result!=RADIOLIB_ERR_NONE||stopped!=RADIOLIB_ERR_NONE||!ready){error("amplitude_test_failed",result);return;}
        Serial.printf("{\"ok\":true,\"kind\":\"amp_done\",\"modulation\":\"%s\",\"frequency_hz\":%lu}\n",
                      ask?"ASK":"OOK",static_cast<unsigned long>(frequency));
        return;
    }
    if(count==9&&!strcmp(words[0],"SET")){
        uint32_t frequency,bw,sf,cr,preamble,sync,inverted;
        char* end=nullptr;errno=0;const long power=strtol(words[5],&end,10);
        if(errno||!end||*end||power< -9||power>0||!unsigned_value(words[1],510000000,frequency)||frequency<470000000||
           !unsigned_value(words[2],500000,bw)||(bw!=125000&&bw!=250000&&bw!=500000)||
           !unsigned_value(words[3],12,sf)||sf<7||!unsigned_value(words[4],8,cr)||cr<5||
           !unsigned_value(words[6],64,preamble)||preamble<6||!unsigned_value(words[7],255,sync)||
           !unsigned_value(words[8],1,inverted)){error("invalid_configuration");return;}
        Config next=config;next.frequency=frequency;next.bandwidth=bw;next.sf=sf;next.cr=cr;next.power=power;next.preamble=preamble;next.sync=sync;next.inverted=inverted;next.gfsk=false;
        const auto result=configure(next);
        if(result!=RADIOLIB_ERR_NONE){ready=configure(config)==RADIOLIB_ERR_NONE;error("radio_configuration_failed",result);return;}
        config=next;ready=true;status();return;
    }
    if(count==2&&!strcmp(words[0],"GFSK")){
        uint32_t frequency;
        if(!unsigned_value(words[1],510000000,frequency)||frequency<470000000){error("invalid_configuration");return;}
        Config next=config;next.frequency=frequency;next.power=-9;next.gfsk=true;
        const auto result=configure(next);
        if(result!=RADIOLIB_ERR_NONE){ready=configure(config)==RADIOLIB_ERR_NONE;error("radio_configuration_failed",result);return;}
        config=next;ready=true;status();return;
    }
    if(count==3&&!strcmp(words[0],"TX")){
        uint32_t id,length;
        if(!ready){error("radio_not_ready");return;}
        if(!unsigned_value(words[1],0xffffffff,id)||!unsigned_value(words[2],sizeof(payload),length)||length<8){error("invalid_tx_request");return;}
        const uint32_t elapsed=millis()-last_tx_ms;
        if(elapsed<cooldown_ms){Serial.printf("{\"ok\":false,\"error\":\"cooldown\",\"remaining_ms\":%lu}\n",static_cast<unsigned long>(cooldown_ms-elapsed));return;}
        const uint32_t airtime=radio.getTimeOnAir(length);
        if(airtime>1000000){error("airtime_exceeds_one_second");return;}
        payload[0]='A';payload[1]='M';payload[2]='C';payload[3]='1';
        for(unsigned i=0;i<4;++i)payload[4+i]=(id>>(8*i))&255;
        uint32_t random=id^0x9e3779b9,hash=2166136261UL;
        for(size_t i=8;i<length;++i){random^=random<<13;random^=random>>17;random^=random<<5;payload[i]=random&255;}
        for(size_t i=0;i<length;++i)hash=(hash^payload[i])*16777619UL;
        tx_done=false;tx_start_us=micros();
        const auto result=radio.startTransmit(payload,length);
        if(result!=RADIOLIB_ERR_NONE){error("start_transmit_failed",result);return;}
        transmitting=true;tx_id=id;tx_length=length;tx_airtime_us=airtime;payload_hash=hash;++started_count;last_result=1;last_tx_ms=millis();cooldown_ms=max(uint32_t(2000),uint32_t((uint64_t(airtime)*10+999)/1000));
        Serial.printf("{\"ok\":true,\"kind\":\"tx_started\",\"id\":%lu,\"length\":%lu,\"airtime_us\":%lu,\"payload_fnv1a32\":%lu,\"start_us\":%lu}\n",static_cast<unsigned long>(id),static_cast<unsigned long>(length),static_cast<unsigned long>(airtime),static_cast<unsigned long>(hash),static_cast<unsigned long>(tx_start_us));return;
    }
    error("unknown_command");
}
void setup(){
    Serial.setRxBufferSize(1024);Serial.setTxBufferSize(2048);Serial.setTxTimeoutMs(1000);
    Serial.begin(115200);
    for(int cs:{21,36,38,39}){pinMode(cs,OUTPUT);digitalWrite(cs,HIGH);}
    pinMode(42,OUTPUT);digitalWrite(42,LOW);
    Wire.begin(3,2);Wire.setClock(100000);
    expander_ready=enable_radio_power();SPI.begin(35,33,34);
    ready=expander_ready&&configure(config)==RADIOLIB_ERR_NONE;
    // 上电不发射；必须收到完整 TX 命令才开始单次发送。
}
void loop(){
    while(Serial.available()){
        const char ch=Serial.read();if(ch=='\r')continue;
        if(ch=='\n'){
            if(overflow)error("line_too_long");else{command[command_length]=0;dispatch(command);}
            command_length=0;overflow=false;
        }else if(ch=='\0'||command_length+1>=sizeof(command))overflow=true;
        else if(!overflow)command[command_length++]=ch;
    }
    if(transmitting&&tx_done){
        tx_done=false;const auto irq=radio.getIrqFlags();auto result=radio.finishTransmit();transmitting=false;
        if(!(irq&RADIOLIB_SX126X_IRQ_TX_DONE))result=RADIOLIB_ERR_TX_TIMEOUT;
        last_result=result;
        if(result==RADIOLIB_ERR_NONE){++completed_count;completed_id=tx_id;completed_elapsed=tx_irq_us-tx_start_us;}
        Serial.printf("{\"ok\":%s,\"kind\":\"tx_done\",\"id\":%lu,\"elapsed_us\":%lu,\"code\":%d}\n",result==RADIOLIB_ERR_NONE?"true":"false",static_cast<unsigned long>(tx_id),static_cast<unsigned long>(tx_irq_us-tx_start_us),result);
    }else if(transmitting&&uint32_t(micros()-tx_start_us)>tx_airtime_us*2+200000){radio.standby();transmitting=false;tx_done=false;last_result=RADIOLIB_ERR_TX_TIMEOUT;error("tx_watchdog_timeout");}
    delay(1);
}
