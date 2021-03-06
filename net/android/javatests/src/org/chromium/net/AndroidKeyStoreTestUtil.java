// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import android.os.Build;
import android.util.Log;

import java.security.PrivateKey;
import java.security.PrivateKey;
import java.security.Signature;
import java.security.KeyFactory;
import java.security.spec.KeySpec;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.KeyStoreException;
import java.security.spec.InvalidKeySpecException;
import java.security.NoSuchAlgorithmException;

import org.chromium.base.CalledByNative;
import org.chromium.base.JNINamespace;
import org.chromium.net.PrivateKeyType;

@JNINamespace("net::android")
public class AndroidKeyStoreTestUtil {

    private static final String TAG = "AndroidKeyStoreTestUtil";

    /**
     * Called from native code to create a PrivateKey object from its
     * encoded PKCS#8 representation.
     * @param type The key type, accoding to PrivateKeyType.
     * @return new PrivateKey handle, or null in case of error.
     */
    @CalledByNative
    public static PrivateKey createPrivateKeyFromPKCS8(int type,
                                                       byte[] encoded_key) {
        String algorithm = null;
        switch (type) {
            case PrivateKeyType.RSA:
                algorithm = "RSA";
                break;
            case PrivateKeyType.DSA:
                algorithm = "DSA";
                break;
            case PrivateKeyType.ECDSA:
                algorithm = "EC";
                break;
            default:
                return null;
        }

        try {
            KeyFactory factory = KeyFactory.getInstance(algorithm);
            KeySpec ks = new PKCS8EncodedKeySpec(encoded_key);
            PrivateKey key = factory.generatePrivate(ks);
            return key;

        } catch (NoSuchAlgorithmException e) {
            Log.e(TAG, "Could not create " + algorithm + " factory instance!");
            return null;
        } catch (InvalidKeySpecException e) {
            Log.e(TAG, "Could not load " + algorithm + " private key from bytes!");
            return null;
        }
    }
}
