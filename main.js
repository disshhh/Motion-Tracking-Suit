import * as THREE from 'three';
import { GLTFLoader } from 'three/examples/jsm/loaders/GLTFLoader';
import { OrbitControls } from 'three/examples/jsm/controls/OrbitControls';
import { RGBELoader } from 'three/examples/jsm/loaders/RGBELoader';

const scene = new THREE.Scene();
new RGBELoader().load('/textures/park_parking_4k.hdr', function (texture) {
    texture.mapping = THREE.EquirectangularReflectionMapping;
    scene.background = texture;
    scene.environment = texture;
});

const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.1, 1000);
camera.position.set(0, 2, 5);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setSize(window.innerWidth, window.innerHeight);
renderer.outputEncoding = THREE.sRGBEncoding;
document.body.appendChild(renderer.domElement);

const light = new THREE.DirectionalLight(0xffffff, 1);
light.position.set(5, 10, 5);
scene.add(light);

// Dictionary to hold bone references
const boneMap = {};
const labelToBoneName = {
    RFA: "mixamorigRightForeArm",
    RA: "mixamorigRightArm",
    LA: "mixamorigLeftArm",
    LFA: "mixamorigLeftForeArm",
    LUL: "mixamorigLeftUpLeg",
    LL: "mixamorigLeftLeg",
    RUL: "mixamorigRightUpLeg",
    RL: "mixamorigRightLeg",
    SP: "mixamorigSpine",
    SP1: "mixamorigSpine1",
    SP2: "mixamorigSpine2",
    H: "mixamorigHead"
};

// Load Model
let model;
new GLTFLoader().load('/ybot.gltf', (gltf) => {
    model = gltf.scene;

    model.traverse((child) => {
        if (child.isBone && labelToBoneName) {
            for (const [label, boneName] of Object.entries(labelToBoneName)) {
                if (child.name === boneName) {
                    boneMap[label] = child;
                    console.log(`✅ Mapped label ${label} to bone ${boneName}`);
                }
            }
        }
    });

    model.scale.set(2, 2, 2);
    scene.add(model);
    console.log("✅ Model added to scene");
});

// WebSocket connections — map each sensor label to its IP
const sensorSockets = {
    RFA: "192.168.193.195",
    RA: "192.168.193.85",
    // Add others:
    // LA: "192.168.193.xx",
    // LFA: "192.168.193.xx",
    // LUL: "192.168.193.xx",
    // LL: "192.168.193.xx",
    // RUL: "192.168.193.xx",
    // RL: "192.168.193.xx",
    // SP: "192.168.193.xx",
    // SP1: "192.168.193.xx",
    // SP2: "192.168.193.xx",
    // H: "192.168.193.xx"
};

function connectSensor(label, ip) {
    const ws = new WebSocket(`ws://${ip}:81`);
    ws.onopen = () => console.log(`✅ WebSocket connected for ${label}`);
    ws.onerror = (err) => console.error(`❌ WebSocket error (${label}):`, err);
    ws.onclose = () => {
        console.warn(`⚠️ WebSocket ${label} closed. Reconnecting in 3s...`);
        setTimeout(() => connectSensor(label, ip), 3000);
    };
    ws.onmessage = (event) => {
        try {
            const data = JSON.parse(event.data);
            if (!Array.isArray(data.quaternion) || data.quaternion.length !== 4) return;

            const bone = boneMap[data.label];
            if (!bone) {
                console.warn(`⚠️ Bone not found for label ${data.label}`);
                return;
            }

            const [w, x, y, z] = data.quaternion;
            const q = new THREE.Quaternion(x, y, z, w);
            bone.quaternion.slerp(q, 0.25); // Smoothly interpolate
        } catch (err) {
            console.error(`❌ Failed to parse message for ${label}`, err);
        }
    };
}

// Start all connections
for (const [label, ip] of Object.entries(sensorSockets)) {
    connectSensor(label, ip);
}

// Controls and animation loop
const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;

function animate() {
    requestAnimationFrame(animate);
    controls.update();
    renderer.render(scene, camera);
}

animate();

window.addEventListener('resize', () => {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
});
