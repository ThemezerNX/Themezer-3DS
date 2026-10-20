# The README's screenshots, looking only: nothing is installed or deleted. They're taken on a console, for its font
# and the icons' colours: this file goes to /3ds/themezer-3ds/autopilot.txt, out/dev/themezer-3ds.3dsx is sent with
# 3dslink, and the shots in /3ds/themezer-3ds/autopilot/ go to docs/screenshots.

wait 240                # the libraries load
touch 95 15             # the SD card's Themes tab, wherever the app was last time
wait 40
repeat 5                # a few themes past the one in use
press DOWN
end
wait 240                # until its blurred preview is behind it
shot themes
press Y                 # the theme on the HOME Menu
wait 90
shot preview
press B
wait 30
press R
press SELECT            # splash screens online
wait 600                # the first page, and the first item's blurred preview
shot splashes
press R
wait 600
shot badges
press SELECT            # back to the SD card's themes, where it opens next time
press R
wait 30
quit
