package com.example.packageguardapp
import retrofit2.Call
import retrofit2.http.*

interface ApiService {
    @FormUrlEncoded
    @POST("/api/login")
    fun loginUser(@Field("login") l: String, @Field("password") p: String): Call<LoginResponse>

    @FormUrlEncoded
    @POST("/api/claim")
    fun claimDeviceMobile(@Field("mac") m: String, @Field("name") n: String, @Query("user") u: String): Call<Void>

    @GET("/api/devices")
    fun getDevices(@Query("user") user: String): Call<List<DeviceData>>

    @GET("/api/cmd/{mac}/{command}")
    fun sendCommand(@Path("mac") mac: String, @Path("command") cmd: String, @Query("user") u: String): Call<Void>
}