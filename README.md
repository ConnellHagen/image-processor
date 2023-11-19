# Image Processor

## About
This project is a multithreaded program that allows for the processing of many images in parallel. The Makefile provides a `make test` command that runs the program on one of the test directories img/0 through img/10. All images within the folder are flipped on their y axis and placed in the output directory. You can change the 180 in the makefile to 270 to cause the images to be flipped on their x and y axes, instead.

This program was a school project for my Intro to Operating Systems (CSCI 4061) class. We wrote this program to practice using the POSIX library's thread and condition variable functionalities. We also had to practice writing some sort of data structure for this to work (I wrote a queue maintained as a linked list for the processing request queue).

## Example:
