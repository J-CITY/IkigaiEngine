package com.daniil.cross_test;

import android.content.res.AssetManager;
import android.os.Bundle;
import org.libsdl.app.SDLActivity;

public class MainActivity extends SDLActivity {
    private native void CreateObjectNative(AssetManager assetManager, String pathToInternalDir);
    private native void DeleteObjectNative();

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        AssetManager assetManager = getAssets();
        String pathToInternalDir = getFilesDir().getAbsolutePath();
        CreateObjectNative(assetManager, pathToInternalDir);
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        // We are exiting the activity, let's delete the native objects
        DeleteObjectNative();

    }

    @Override
    protected String[] getLibraries() {
        return new String[]{
                "SDL3",
                "cross_test"
        };
    }
}
