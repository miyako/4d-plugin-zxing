//%attributes = {}
$file:=Folder:C1567(fk desktop folder:K87:19).file("調整後.jpg")

READ PICTURE FILE:C678($file.platformPath; $image)

$status:=zxing decode($image)