# Archives natives de release

`native/0.15.0.tgz` contient les programmes et le kext PowerPC construits
avec Xcode 1.5 sur Mac OS X 10.3.9. L'empreinte SHA-256 est figée dans
`native/0.15.0.tgz.sha256` ; le workflow refuse toute différence.

Les machines GitHub Actions ne compilent pas ce code Panther. Pour une
nouvelle version, compiler dans la VM avec `scripts/build-vm.sh`, tester sur
une vraie clé, préparer la nouvelle archive native et son empreinte, puis
mettre à jour `Info.plist` et les scripts d'installation. Le générateur
`scripts/package-driver.py --version X.Y.Z` refuse une divergence de version.

Le tag `vX.Y.Z` déclenche la publication du kit installable et de ses
empreintes. Ajouter aussi `notes/X.Y.Z.md` pour la page de release. Aucun
secret de réseau ne doit entrer dans cette archive native.
