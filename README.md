# MPEG Player

MPEG 224x112

```console
ffmpeg -i input.mp4 -c:v mpeg1video -q:v 0 -c:a mp2 -ab 224k -vf "fps=25,scale=224:112:flags=lanczos" -y output.mpg
```

## MPEG 320x180

```console
ffmpeg -i input.mp4 -c:v mpeg1video -vb 320k -c:a mp2 -ab 64k -vf "fps=25,scale=320:180:flags=lanczos" -y output.mpg
```
