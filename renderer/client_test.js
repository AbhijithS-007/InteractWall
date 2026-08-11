const net = require('net');

const client = net.createConnection({ path: '\\\\.\\pipe\\InteractWall' }, () => {
    console.log('Connected to InteractWall IPC server!');

    // Test get_status
    client.write(JSON.stringify({ cmd: 'get_status' }) + '\n');

    // Wait a sec then test apply_wallpaper
    setTimeout(() => {
        const msg = {
            cmd: "apply_wallpaper",
            layerA: "C:\\My_Proj\\InteractWall\\wallpapers\\Witcher.jpg",
            layerB: "C:\\My_Proj\\InteractWall\\wallpapers\\frieren-magical.jpeg"
        };
        client.write(JSON.stringify(msg) + '\n');
        console.log("Sent apply_wallpaper");
        
        setTimeout(() => client.end(), 1000);
    }, 500);
});

client.on('data', (data) => {
    console.log('Received response from server:', data.toString());
});

client.on('end', () => {
    console.log('Disconnected from server');
});

client.on('error', (err) => {
    console.error('Error:', err.message);
});
