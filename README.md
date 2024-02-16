# MPEG Player

MPEG 208x112

```console
ffmpeg -i input.mp4 -c:v mpeg1video -q:v 0 -c:a mp2 -ab 224k -vf "fps=25,scale=208:112:flags=lanczos" -y output.mpg
```
