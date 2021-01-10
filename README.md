# ying-bend

This an application that will work on yoga-book, look at the acceloremetrs and :

  * autorotate the screen so it will always be upright.
  
  * disable the halo keyboard when it is bended in tablet mode.

This works atleast on :

  * YB1-X91L

We write it in C so it will be very efficent because it runs once a second all the time.


There are two applications :

  * autorotate - will do autorotate and determine the laptop/tablet switch.

  * ltSwitch - will just do laptop/tablet switch.


# Install

You have to have the xrandr and xinput2 extensions to X11

So for debian you should:

    apt-get install build-essential libxrandr-dev libxi-dev

To build, just run make
And execute, on startup maybe, in your X11 session.
