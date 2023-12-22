# Image Processor

## About
This project is a multithreaded program that allows for the processing of many images in parallel. It uses a client-server model where the client creates each
request, sends it to the server, and the server sends back the handle data.
The Makefile provides a `make test` command that runs the program on one of the test directories img/0 through img/10. All images within the folder are flipped on their y axis and placed in the output directory. You can change the 180 in the makefile to 270 to cause the images to be flipped on their x and y axes, instead.
Alternatively, run `./server` in one terminal, and create your requests in another terminal using
```bash
./client <input folder> <output folder> <180 or 270>
```

This program was a school project for my Intro to Operating Systems (CSCI 4061) class. We wrote this program to practice using the POSIX library's thread and condition variable functionalities. We also had to practice writing some sort of data structure for this to work (I wrote a queue maintained as a linked list for the processing request queue).

## Example:
|img/4/4493.png|output/4/rotated4493.png|
|--------------|------------------------|
|![image](https://github.com/ConnellHagen/image-processor/assets/72321241/f7351cd6-ebb5-49ab-9b2c-8666d60be88d)|![image](https://github.com/ConnellHagen/image-processor/assets/72321241/16913892-8204-497a-b2a6-ab3e732a3813)|

