#ifdef FAKE
#include "stepperControl/mockgpio.h"
#else
#include "stepperControl/gpio.h"
#endif

#include "configreader.h"
#include "controller.h"
#include "log.h"
#include "model.h"

#include <iostream>
#include <sysexits.h>

int main(int argc, char* argv[])
{
    try {
        INIT_MGOLOG("lc.log");
        MGOLOG("Program started");

        std::string configFile = "lc.cfg";
        if (argc > 1) {
            if (argv[1][0] == '-') {
                std::cout << "\nUsage: lc <configfile>\n\n";
                return EX_USAGE;
            }
            configFile = argv[1];
        }

        mgo::ConfigReader config(configFile);

        // clang-format off
        #ifdef FAKE
            mgo::MockGpio gpio(false, config);
        #else
            mgo::Gpio gpio;
        #endif
        // clang-format on

        mgo::Model model(gpio, config);

        mgo::Controller controller(&model);
        controller.run();

        return EX_OK;
    } catch (const std::exception& e) {
        std::cout << "Exception encountered: " << e.what() << std::endl;
        return EX_SOFTWARE;
    }
}
