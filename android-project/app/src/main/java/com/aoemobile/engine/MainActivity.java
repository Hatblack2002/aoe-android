package com.aoemobile.engine;

import android.app.Activity;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Actividad principal del motor (M1).
 *
 * Extiende SDLActivity para anadir el importador de archivos (spec §6 M1:
 * el usuario trae sus archivos reales del juego al telefono sin permisos de
 * almacenamiento, usando el selector de documentos de Android / SAF).
 * El flujo:
 *   1. El codigo nativo llama startFilePickerFromNative() via JNI.
 *   2. Se abre ACTION_OPEN_DOCUMENT con seleccion multiple.
 *   3. onActivityResult copia cada archivo a files/import/<nombre>.
 *   4. Se avisa al nativo con nativeOnFilesImported(n) para re-chequear.
 */
public class MainActivity extends org.libsdl.app.SDLActivity {

    private static final int PETICION_ARCHIVOS = 4242;
    private static final String ETIQUETA = "aoe-mobile";

    /** Llamado desde C++ (reflexion). Abre el selector en el hilo de UI. */
    public void startFilePickerFromNative() {
        final Activity actividad = this;
        runOnUiThread(new Runnable() {
            @Override
            public void run() {
                try {
                    Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                    i.addCategory(Intent.CATEGORY_OPENABLE);
                    i.setType("*/*");
                    i.putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true);
                    actividad.startActivityForResult(i, PETICION_ARCHIVOS);
                } catch (Exception e) {
                    Log.e(ETIQUETA, "selector no disponible", e);
                }
            }
        });
    }

    @Override
    protected void onActivityResult(int peticion, int resultado, Intent datos) {
        super.onActivityResult(peticion, resultado, datos);
        if (peticion != PETICION_ARCHIVOS || resultado != RESULT_OK || datos == null) return;

        int copiados = 0;
        File destino = getExternalFilesDir(null);
        if (destino == null) destino = getFilesDir();
        File importDir = new File(destino, "import");
        if (!importDir.exists() && !importDir.mkdirs()) {
            Log.e(ETIQUETA, "no se pudo crear " + importDir);
            return;
        }

        ContentResolver resolver = getContentResolver();
        if (datos.getClipData() != null) {
            int n = datos.getClipData().getItemCount();
            for (int i = 0; i < n; ++i) {
                if (copiarArchivo(resolver, datos.getClipData().getItemAt(i).getUri(), importDir)) {
                    ++copiados;
                }
            }
        } else if (datos.getData() != null) {
            if (copiarArchivo(resolver, datos.getData(), importDir)) ++copiados;
        }

        Log.i(ETIQUETA, "importados " + copiados + " archivo(s) en " + importDir);
        notificarNativo(copiados);
    }

    /** Copia un Uri a import/<nombre real> y devuelve true si funciono. */
    private boolean copiarArchivo(ContentResolver resolver, Uri uri, File importDir) {
        InputStream entrada = null;
        OutputStream salida = null;
        try {
            String nombre = nombreDeArchivo(resolver, uri);
            if (nombre == null || nombre.isEmpty()) nombre = "archivo_importado";
            File salidaFile = new File(importDir, nombre);
            entrada = resolver.openInputStream(uri);
            if (entrada == null) return false;
            salida = new FileOutputStream(salidaFile);
            byte[] bufer = new byte[64 * 1024];
            int leidos;
            while ((leidos = entrada.read(bufer)) > 0) salida.write(bufer, 0, leidos);
            salida.flush();
            Log.i(ETIQUETA, "copiado: " + salidaFile.getAbsolutePath());
            return true;
        } catch (Exception e) {
            Log.e(ETIQUETA, "fallo al copiar " + uri, e);
            return false;
        } finally {
            try { if (entrada != null) entrada.close(); } catch (Exception ignorada) {}
            try { if (salida != null) salida.close(); } catch (Exception ignorada) {}
        }
    }

    /** Nombre visible del documento (OpenableColumns) con reserva. */
    private String nombreDeArchivo(ContentResolver resolver, Uri uri) {
        Cursor c = null;
        try {
            c = resolver.query(uri, null, null, null, null);
            if (c != null && c.moveToFirst()) {
                int col = c.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                if (col >= 0) return c.getString(col);
            }
        } catch (Exception ignorada) {
        } finally {
            if (c != null) c.close();
        }
        return "archivo_importado";
    }

    /** Avisa al codigo nativo (libmain.so) de que la importacion termino. */
    private static native void nativeOnFilesImported(int cuantos);

    private void notificarNativo(int copiados) {
        try {
            nativeOnFilesImported(copiados);
        } catch (UnsatisfiedLinkError e) {
            Log.e(ETIQUETA, "nativeOnFilesImported no enlazada", e);
        }
    }
}
