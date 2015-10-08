package com.aupeo.gplayer;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.net.NetworkInfo;
import android.util.Log;

public class ConnectionChangeReceiver extends BroadcastReceiver
{
    @Override
    public void onReceive( Context context, Intent intent )
    {
        GPlayer.getInstance().networkChanged();
        NetworkInfo ni = GPlayerConnectivity.getNetworkInfo(context);
        if (ni != null) {
            Log.d("GPlayer", "FAST: " + GPlayerConnectivity.isConnectionFast(ni.getType(), ni.getSubtype()) + " TYPE: " + ni.getSubtypeName());
        } else {
            Log.d("GPlayer", "no network!");
        }
    }
}
