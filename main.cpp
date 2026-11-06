#ifdef FAKE
#include "stepperControl/mockgpio.h"
#else
#include "stepperControl/gpio.h"
#endif

#include "configreader.h"
#include "controller.h"
#include "log.h"
#include "model.h"

#include <QApplication>

#include <iostream>
#include <sysexits.h>

int main(int argc, char* argv[])
{
    try {
        // Qt on macOS normally swaps Ctrl and Cmd; keep Ctrl as Ctrl so the
        // key bindings (e.g. Ctrl-Q) are the same on every platform
        QCoreApplication::setAttribute(Qt::AA_MacDontSwapCtrlAndMeta);
        // Must exist before any UI is created, and outlive it. (Note this
        // removes any Qt-specific arguments, such as -platform, from argv)
        QApplication app(argc, argv);

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
            mgo::MockGpioOptions mockOptions;
            mockOptions.rotaryDirectionNormal
                = config.readBool("MockRotaryDirectionNormal", false);
            mockOptions.rotaryEncoderDelayMicroseconds
                = config.readLong("MockRotaryEncoderDelayMicroseconds", 250);
            mockOptions.linearScaleAxis1StepsPerMm
                = config.readLong("LinearScaleAxis1StepsPerMM", 200);
            mgo::MockGpio gpio(false, mockOptions);
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
