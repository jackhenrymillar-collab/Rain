# Installing Rain on a Mac

1. Unzip `Rain-macOS.zip`.
2. Copy `Rain.vst3` to `~/Library/Audio/Plug-Ins/VST3/`
   and `Rain.component` to `~/Library/Audio/Plug-Ins/Components/`.
   (In Finder: Go › Go to Folder…, then paste the path.)
3. These builds are not notarised by Apple, so macOS blocks files downloaded from
   the internet. Clear that once in Terminal:

   ```sh
   xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Rain.vst3 ~/Library/Audio/Plug-Ins/Components/Rain.component
   ```

4. Restart your DAW. In Logic, rescan with Settings › Plug-in Manager if Rain
   doesn't appear.
