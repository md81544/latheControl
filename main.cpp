#include "steppermotor.h"
#include "gpio.h"

#include <iostream>

int main()
{
    try
    {
        mgo::Gpio gpio( 8, 7 ); // step pin, reverse pin

        mgo::StepperMotor motor( gpio, 1'000 );

        long step = 250;
        for ( int n = 0; n < 4; ++n )
        {
            motor.goToStep( step );
            motor.wait();
            gpio.delayMicroSeconds( 1'000'000 );
            step += 250;
        }
        return 0;
    }
    catch( const std::exception& e )
    {
        std::cout << "Exception encountered: " << e.what() << std::endl;
        return 1;
    }
}
