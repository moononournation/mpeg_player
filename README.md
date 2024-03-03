# MPEG Player

MPEG 224x128

```console
ffmpeg -i input.mp4 -c:v mpeg1video -vb 160k -c:a mp2 -ab 64k -vf "fps=25,scale=224:128:flags=lanczos,crop=224:128:(in_w-224)/2:0" -y 224x128.mpg
```

## MPEG 320x240

```console
ffmpeg -i input.mp4 -c:v mpeg1video -vb 320k -c:a mp2 -ab 64k -vf "fps=25,scale=-1:240:flags=lanczos,crop=320:240:(in_w-320)/2:0" -y 320x240.mpg
```
