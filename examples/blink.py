#
# Copyright © 2019 Keith Packard <keithp@keithp.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#

led = 13

def up():
    for i in range(0,1,0.01):
        setpower(i)
        time.sleep(0.001)

def down():
    for i in range(1,0,-0.01):
        setpower(i)
        time.sleep(0.001)

def blink():
    talkto(led)
    setpower(0)
    on()
    while True:
        up()
        down()

blink()

