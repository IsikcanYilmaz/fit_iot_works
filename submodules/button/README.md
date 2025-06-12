Button handling for CCN_NC demo boards. Run Button_Init to start the button handling thread and pass the pid of the thread that will be on the receiving end of button events. The module handles debounce and gesture recognition logics. 

The thread will toss button event messages to a thread that will process them. The messages will contain the following info:

- Which button was hit
- If shift was held while press happened
- Which gesture happened

