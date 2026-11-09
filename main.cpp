#ifdef FAKE
#include "stepperControl/mockgpio.h"
#else
#include "stepperControl/gpio.h"
#endif

#include "configreader.h"
#include "controller.h"
#include "cpuaffinity.h"
#include "log.h"
#include "model.h"

#include <QApplication>

#include <filesystem>
#include <iostream>
#include <set>
#include <string>
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

        // A fresh log each session, keeping the last session's (replacing the
        // one before). Not being able to (e.g. no log yet) doesn't matter
        std::error_code ignored;
        std::filesystem::rename("lc.log", "lc.log.previous", ignored);
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

        // Keep everything that's running so far (including pigpio's threads,
        // which must not be starved of CPU or encoder pulses are lost) off any
        // cores reserved for the motor threads, which are pinned to them later
        std::set<int> motorCores;
        for (const std::string axis : { "Axis1", "Axis2" }) {
            if (const std::string core = config.read(axis + "CpuCore", ""); !core.empty()) {
                motorCores.insert(std::stoi(core));
            }
        }
        if (!motorCores.empty()) {
            if (mgo::keepThreadsOffCores(motorCores)) {
                MGOLOG("Other threads kept off the motors' CPU cores");
            } else {
                MGOLOG("*** Warning *** couldn't keep other threads off the motors' CPU cores");
            }
        }

        mgo::Model model(gpio, config);

        mgo::Controller controller(&model);
        controller.run();

        return EX_OK;
    } catch (const std::exception& e) {
        std::cout << "Exception encountered: " << e.what() << std::endl;
        return EX_SOFTWARE;
    }
}
