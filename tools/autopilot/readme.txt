# The README's screenshots, looking only: nothing is installed or deleted. They're taken on a console, for its font
# and the icons' colours: this file goes to /3ds/themezer-3ds/autopilot.txt, out/dev/themezer-3ds.3dsx is sent with
# 3dslink, and the shots in /3ds/themezer-3ds/autopilot/ go to docs/screenshots. All three are of the SD card's tabs.

wait 240                # the libraries load
touch 95 15             # the SD card's Themes tab, wherever the app was last time
wait 40
repeat 5                # a few themes past the one in use
press DOWN
end
wait 240                # until its blurred preview is behind it
shot themes
press R                 # the SD card's splash screens
wait 240
shot splashes
press R                 # the SD card's badges
wait 300
shot badges
touch 95 15             # back to the themes, where it opens next time
wait 30
quit
