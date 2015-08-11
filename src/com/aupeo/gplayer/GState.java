package com.aupeo.gplayer;

public enum GState {
        GST_STATE_VOID_PENDING(0),
        GST_STATE_NULL(1),
        GST_STATE_READY(2),
        GST_STATE_PAUSED(3),
        GST_STATE_PLAYING(4),
        GST_STATE_PREPARED(5);
        
        private int prio;

        GState(int prio) {
            this.prio = prio;
        }
}
