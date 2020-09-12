# Photon-utility-watch
==========

This repo has (a) code from the Particle.io IDE and (b) hardware details, for a
simple system that monitors activity of a residential home sump pump, a water heater, and hvac system.

##1. Software

Paricle code - I use the Photon (wifi) and the Particle.io PUBLISH function,
then Webhooks at Particle.io to push stuff to ThingSpeak.com.
There I graph things and can set triggers.
It's a bit of a Rube Goldberg but it's been solid since 2017...

The code runs a reporting loop that every 30s or reports one of five different values (one at a time so as not to exceed the API throttles).  Three values are just what the sensors are telling us, so I can look at these and fiddle with thresholds (see hardware below) to decide when something is "on" or "off."  I also report the duration of sump and hvac 'events' so I am keeping track of how long these things run, not just how often.

While the reporting loop is running I have three interrupt timers to do that measurements
(and determine things like duration).
One timer checks the sump every 2 seconds (since its typical runtime is 15-25s);
another checks both the HVAC and water heater, and tracks how many times the sump runs in a 30min window
(a lot means I should be paying attention just in case).  This one runs about 4x a minute, which is plenty often).
A third interrupt runs about every 30 minutes and checks to see if the sump is running hard.  If so then
that's published and through webhooks it triggers a text message letting me know to keep an eye on things.

Partly because I am not sure how robust the Photon is to simultaneous interrupts I set these three timers
with prime numbers so they won't collide much, and because it seemed like a fun use of prime numbers.

##2. Hardware

On the Photon I have three sensors attached to three pins.

### Current Sensor

For the sump pump I have a Hall Effect [current sensor](https://moderndevice.com/product/current-sensor/) which zip-ties to the power cord and measures power (Watts) from 0-1400 with a nice linear output from 0-4v.  

### HVAC Sensor

As it turns out there is an A/C outlet on the side of most furnaces that is only live when the blower is running.  I took an old DC power supply (wall wort) and stepped it down to 4v, then attached to a pin on the Photon.  When the blower turns on (furnace or AC) the outlet goes live and my voltage to the pin goes from 0 to 4.

### Water Heater

Mine is gas so I stuck a pretty sturdy one-wire DS18B20 temperature sensor like [this one](https://www.dx.com/p/waterproof-ds18b20-temperature-sensor-with-adapter-module-for-arduino-2068262.html?tc=USD&ta=US) which is nice in that it comes with a breakout board.  As with the other sensors, you attach this to a Photon pin.  In my case the water heater is right next to the furnace so they share a chimney.  That means when the furnace is firing the water heater chimney also heats up, but not as much as when the water heater is going.  So you have to play with the thresholds to get it right for your situation.
