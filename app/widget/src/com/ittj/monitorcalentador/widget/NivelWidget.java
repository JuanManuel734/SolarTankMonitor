package com.ittj.monitorcalentador.widget;

/*
 * Widget de pantalla de inicio del Monitor del calentador solar.
 *
 *  - Se actualiza solo cada 30 min (mínimo de Android) y con el botón ↻.
 *  - Consulta http://<host>/data del ESP32; el host lo guarda la app Delphi
 *    en SharedPreferences ("monitor_calentador_widget", clave "host").
 *  - La app Delphi también guarda cada lectura que recibe y envía la acción
 *    DATOS, así el widget se refresca sin hacer otra petición.
 *  - Los recursos (layout, ids) se buscan por nombre con getIdentifier, porque
 *    la clase R la genera Delphi al compilar la app.
 */

import android.app.PendingIntent;
import android.appwidget.AppWidgetManager;
import android.appwidget.AppWidgetProvider;
import android.content.BroadcastReceiver;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.res.Resources;
import android.widget.RemoteViews;

import org.json.JSONObject;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.HttpURLConnection;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;

public class NivelWidget extends AppWidgetProvider {

    public static final String PREFS = "monitor_calentador_widget";
    public static final String ACCION_REFRESCAR = "com.ittj.monitorcalentador.widget.REFRESCAR";
    public static final String ACCION_DATOS = "com.ittj.monitorcalentador.widget.DATOS";

    @Override
    public void onReceive(Context context, Intent intent) {
        String accion = intent.getAction();
        if (ACCION_DATOS.equals(accion)) {
            // La app ya guardó la lectura: solo dibujar.
            dibujar(context, null);
            return;
        }
        if (ACCION_REFRESCAR.equals(accion)
                || AppWidgetManager.ACTION_APPWIDGET_UPDATE.equals(accion)) {
            dibujar(context, "Actualizando…");
            final PendingResult pendiente = goAsync();
            final Context app = context.getApplicationContext();
            new Thread(new Runnable() {
                @Override
                public void run() {
                    try {
                        consultar(app);
                    } finally {
                        dibujar(app, null);
                        pendiente.finish();
                    }
                }
            }).start();
            return;
        }
        super.onReceive(context, intent);
    }

    // ------------------------------------------------------------------
    // Consulta al ESP32
    // ------------------------------------------------------------------
    private static void consultar(Context ctx) {
        SharedPreferences p = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        String host = p.getString("host", "").trim();
        if (host.length() == 0) {
            p.edit().putString("error", "Abre la app y escribe la IP del ESP32").apply();
            return;
        }
        String base = host.startsWith("http://") || host.startsWith("https://") ? host : "http://" + host;
        while (base.endsWith("/")) base = base.substring(0, base.length() - 1);

        HttpURLConnection con = null;
        try {
            URL url = new URL(base + "/data");
            con = (HttpURLConnection) url.openConnection();
            con.setConnectTimeout(5000);
            con.setReadTimeout(6000);
            con.setUseCaches(false);
            if (con.getResponseCode() != 200) throw new Exception("HTTP " + con.getResponseCode());
            BufferedReader in = new BufferedReader(new InputStreamReader(con.getInputStream(), "UTF-8"));
            StringBuilder sb = new StringBuilder();
            String linea;
            while ((linea = in.readLine()) != null) sb.append(linea);
            in.close();

            JSONObject d = new JSONObject(sb.toString());
            SharedPreferences.Editor e = p.edit();
            e.putFloat("nivel", (float) d.optDouble("level", 0));
            boolean tempOk = !d.isNull("temperatureC");
            e.putBoolean("tempOk", tempOk);
            if (tempOk) e.putFloat("temp", (float) d.optDouble("temperatureC", 0));
            e.putString("estado", d.optString("state", ""));
            e.putString("hora", new SimpleDateFormat("HH:mm", Locale.getDefault()).format(new Date()));
            e.putBoolean("hayDatos", true);
            e.remove("error");
            e.apply();
        } catch (Exception ex) {
            p.edit().putString("error", "Sin conexión con el ESP32").apply();
        } finally {
            if (con != null) con.disconnect();
        }
    }

    // ------------------------------------------------------------------
    // Dibujo del widget con los datos guardados
    // ------------------------------------------------------------------
    private static int id(Context ctx, String nombre, String tipo) {
        Resources r = ctx.getResources();
        return r.getIdentifier(nombre, tipo, ctx.getPackageName());
    }

    static void dibujar(Context ctx, String aviso) {
        int layout = id(ctx, "widget_nivel", "layout");
        if (layout == 0) return;
        RemoteViews v = new RemoteViews(ctx.getPackageName(), layout);
        SharedPreferences p = ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);

        boolean hayDatos = p.getBoolean("hayDatos", false);
        float nivel = p.getFloat("nivel", 0);
        int nivelColor = nivel <= 0 ? 0xFFE74C3C : (nivel < 50 ? 0xFFE67E22 : (nivel < 80 ? 0xFFF1C40F : 0xFF2ECC71));

        v.setTextViewText(id(ctx, "w_nivel", "id"), hayDatos ? Math.round(nivel) + "%" : "--%");
        v.setTextColor(id(ctx, "w_nivel", "id"), hayDatos ? nivelColor : 0xFF999999);
        v.setProgressBar(id(ctx, "w_barra", "id"), 100, hayDatos ? Math.round(nivel) : 0, false);

        String temp = "-- °C";
        if (hayDatos && p.getBoolean("tempOk", false)) {
            temp = String.format(Locale.getDefault(), "%.1f °C", p.getFloat("temp", 0));
        }
        v.setTextViewText(id(ctx, "w_temp", "id"), temp);

        String info;
        if (aviso != null) {
            info = aviso;
        } else if (p.contains("error")) {
            info = p.getString("error", "") + (hayDatos ? " · última " + p.getString("hora", "") : "");
        } else if (hayDatos) {
            String estado = p.getString("estado", "");
            info = "Actualizado " + p.getString("hora", "") + (estado.length() > 0 ? " · " + estado : "");
        } else {
            info = "Calentador solar · toca ↻";
        }
        v.setTextViewText(id(ctx, "w_info", "id"), info);

        int flags = PendingIntent.FLAG_UPDATE_CURRENT | PendingIntent.FLAG_IMMUTABLE;

        // Tocar el widget abre la app
        Intent abrir = ctx.getPackageManager().getLaunchIntentForPackage(ctx.getPackageName());
        if (abrir != null) {
            v.setOnClickPendingIntent(id(ctx, "w_raiz", "id"), PendingIntent.getActivity(ctx, 0, abrir, flags));
        }

        // Botón ↻
        Intent refrescar = new Intent(ctx, NivelWidget.class).setAction(ACCION_REFRESCAR);
        v.setOnClickPendingIntent(id(ctx, "w_refrescar", "id"), PendingIntent.getBroadcast(ctx, 1, refrescar, flags));

        AppWidgetManager.getInstance(ctx).updateAppWidget(new ComponentName(ctx, NivelWidget.class), v);
    }
}
