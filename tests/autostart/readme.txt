
When GEMDOS HD and/or some of the Hatari fast boot options
(--fast-boot, --fastfdc) are used, autostarted programs may start so
quickly that some TOS versions haven't had time to start _hz200
timer/counter needed by those programs.

That issue is very rare and mainly affects printing, but it's easy to
test whether that's the reason why something doesn't work when being
autostarted. Just put a program doing a small sleep/wait to AUTO/
folder.

Normally TOS v1.x seems to start that counter ~5s after boot (in
*emulated* time), and the boot itself will always take some of that,
so waiting quite that long isn't necessary.

Try first with 4_SECS.PRG (4s wait), and if it fixes the issue, you
could also try whether 2_SECS.PRG (2s wait) is enough for that.

Note: you can use "--fast-forward on" option on boot, as it doesn't
impact emulated Atari time.
