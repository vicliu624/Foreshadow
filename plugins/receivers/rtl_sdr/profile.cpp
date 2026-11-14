#include "profile.hpp"
namespace signalscope {
ReceiverCapabilities rtlReceiverCapabilities(int tuner,const std::vector<int>& gains,std::string_view maker,std::string_view product) {
    ReceiverCapabilities c;c.backend="rtlsdr";c.manual_gains_tenths=gains;c.manual_gains_known=true;
    // librtlsdr exposes tuner identity, but no API to enumerate its RF ranges.
    // Nominal tuner profiles (no direct sampling); not calibrated passbands.
    c.frequency_source="tuner_specification";
    switch(tuner) {
    case 1:c.model="E4000";c.frequency_ranges={{52e6,1.1e9},{1.25e9,2.2e9}};break;
    case 2:c.model="FC0012";c.frequency_ranges={{22e6,948.6e6}};break;
    case 3:c.model="FC0013";c.frequency_ranges={{22e6,1.1e9}};break;
    case 4:c.model="FC2580";c.frequency_ranges={{146e6,308e6},{438e6,924e6}};break;
    case 5:c.model="R820T";c.frequency_ranges={{24e6,1.766e9}};break;
    case 6:c.model="R828D";c.frequency_ranges={{24e6,1.766e9}};break;
    default:c.model="Unknown RTL-SDR tuner";c.frequency_source="unknown";break;
    }
    c.sample_rate_ranges={{225001,300000},{900001,3200000}};
    c.rate_source="librtlsdr_api";
    c.sample_rates={250000,1000000,1024000,1750000,1875000,2000000,2048000,2400000,2560000,2800000,3200000};
    c.recommended_rates={2560000,2400000,2000000};
    // A direct ADC path is a board feature, not an inference from tuner type.
    if(maker=="HackerGadgets" && product.find("AIO_V2")!=std::string_view::npos) {
        c.model="HackerGadgets AIO V2 (R820T/R860)";c.direct_sampling_supported=true;
        c.frequency_ranges={{24e6,1.74e9}};c.frequency_source="board_specification";
        c.direct_sampling_ranges={{100000,28800000}};
    }
    return c;
}
}
