package com.nar;

public class NativeApp
{
    static
    {
        NarSystem.loadLibrary();
    }

//    public final native int sayHello(int jargc, String[] jargv);
    public final native int sayHello(String[] stringArray);

//    public static void main( String[] args )
//    {
//        NativeApp app = new NativeApp();
//        System.out.println( app.sayHello() );
//    }
}
