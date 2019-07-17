#include "steppermotor.h"
#include "gpio.h"

#include <iostream>

int main()
{
    try
    {
        mgo::Gpio gpio( 8, 7 ); // step pin, reverse pin

        for ( int n = 0; n < 1000; ++ n )
        {
            gpio.setStepPin( mgo::PinState::high );
            gpio.delayMicroSeconds( 500 );
            gpio.setStepPin( mgo::PinState::low );
            gpio.delayMicroSeconds( 500 );
        }

        return 0;
    }
    catch( const std::exception& e )
    {
        std::cout << "Exception encountered: " << e.what() << std::endl;
        return 1;
    }
}
