#ifndef FOOTER_H
#define FOOTER_H    
    // **********************************************************************************
    // ** the following code cunfigures the rest of the module please do not change it **
    // ** if you not know exactly what you are doing, it can break the module          **
    // **********************************************************************************

    #ifdef RASPBERRY_PI_SPI
        #pragma message("Build for Raspberry PI SPI communication!")
    #endif

    #define use_stepcounter 0 // Use step counter for the stepgen
    #define debug_mode 0   // only used in Raspberry PI communications
    #define max_statemachines stepgens + encoders
    
    #ifdef PICO_RP2040
        #if max_statemachines > 8
            #error "State machines exceeded the maximum platform size (8)."
...
            #error "State machines exceeded the maximum platform size (12)."
        #endif
    #endif

    #define pico_clock 150000000

#endif