# MPEG Player

MPEG 224x112

```console
ffmpeg -i input.mp4 -c:v mpeg1video -q:v 0 -c:a mp2 -ab 224k -vf "fps=25,scale=224:112:flags=lanczos" -y output.mpg
```

## MPEG 320x240

```console
ffmpeg -i input.mp4 -c:v mpeg1video -vb 320k -c:a mp2 -ab 64k -vf "fps=25,scale=-1:240:flags=lanczos,crop=320:240:(in_w-320)/2:0" -y output.mpg
```
