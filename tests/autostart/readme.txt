
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

Provided AUTO-folder program searches for "\AUTO\??_SLEEP.PRG", and
sleeps number of seconds specified (as "??") in the program name. Try
first with "4s_sleep.prg", and if it fixes the issue, you could also
try whether renaming it "2s_sleep.prg" is enough.

Note: you can use "--fast-forward on" option on boot, as it doesn't
impact emulated Atari time.
