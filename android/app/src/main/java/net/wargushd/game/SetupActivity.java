package net.wargushd.game;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.os.Environment;
import android.provider.DocumentsContract;
import android.widget.Toast;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileWriter;

/**
 * First-run content gate (MANDATORY, per project governance / owner requirement):
 * on first launch the user MUST point the app at their Warcraft II: Remastered files folder.
 * The picked folder is verified (must contain the Remastered "Data" tree) and its resolved
 * filesystem path is persisted to files/wc2_source.txt. Only then does the game
 * (WargusHDActivity) start. On later launches, if the saved source is still valid, the game
 * starts directly with no prompt.
 */
public class SetupActivity extends Activity {
    private static final int REQ_PICK = 1001;

    private File sourceMarker() { return new File(getFilesDir(), "wc2_source.txt"); }

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        if (hasValidSource()) { startGame(); return; }
        launchPicker();
    }

    private boolean hasValidSource() {
        try {
            File m = sourceMarker();
            if (!m.exists()) return false;
            String path = readFile(m).trim();
            return !path.isEmpty() && new File(path, "Data").isDirectory();
        } catch (Exception e) { return false; }
    }

    private void launchPicker() {
        Toast.makeText(this, "Select your Warcraft II: Remastered folder", Toast.LENGTH_LONG).show();
        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        i.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION
                 | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        startActivityForResult(i, REQ_PICK);
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != REQ_PICK) return;
        if (res != RESULT_OK || data == null || data.getData() == null) { launchPicker(); return; }
        Uri tree = data.getData();
        try {
            getContentResolver().takePersistableUriPermission(tree, Intent.FLAG_GRANT_READ_URI_PERMISSION);
        } catch (Exception ignored) {}
        String path = resolveTreePath(tree);
        if (path == null || !new File(path, "Data").isDirectory()) {
            Toast.makeText(this, "That folder has no Remastered 'Data' — pick the WC2 folder",
                           Toast.LENGTH_LONG).show();
            launchPicker(); return;
        }
        try {
            FileWriter w = new FileWriter(sourceMarker()); w.write(path); w.close();
        } catch (Exception e) {
            Toast.makeText(this, "Could not save source path", Toast.LENGTH_LONG).show();
            return;
        }
        Toast.makeText(this, "WC2 files verified: " + path, Toast.LENGTH_LONG).show();
        startGame();
    }

    /** Resolve a SAF tree URI to a real filesystem path (primary storage; best-effort for SD volumes). */
    private String resolveTreePath(Uri tree) {
        try {
            String docId = DocumentsContract.getTreeDocumentId(tree);
            String[] parts = docId.split(":", 2);
            String vol = parts[0];
            String rel = parts.length > 1 ? parts[1] : "";
            if ("primary".equalsIgnoreCase(vol)) {
                return new File(Environment.getExternalStorageDirectory(), rel).getAbsolutePath();
            }
            return "/storage/" + vol + "/" + rel;
        } catch (Exception e) { return null; }
    }

    private void startGame() {
        startActivity(new Intent(this, WargusHDActivity.class));
        finish();
    }

    private String readFile(File f) throws Exception {
        byte[] buf = new byte[(int) f.length()];
        try (FileInputStream in = new FileInputStream(f)) { in.read(buf); }
        return new String(buf, "UTF-8");
    }
}
