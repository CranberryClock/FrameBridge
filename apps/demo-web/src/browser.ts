import * as THREE from "three";
import { WebGPURenderer } from "three/webgpu";
import { browserFallbackStatus } from "./index.js";

const canvas = document.querySelector<HTMLCanvasElement>("#cube");
const mode = document.querySelector<HTMLParagraphElement>("#mode");
if (!canvas || !mode) throw new Error("demo elements missing");
const canvasElement = canvas;
const modeElement = mode;
const webgpu = (navigator as Navigator & { gpu?: unknown }).gpu;

async function main(): Promise<void> {
  modeElement.textContent = `${browserFallbackStatus()} — browser-authoritative simulation frame 0`;
  if (!webgpu) {
    modeElement.textContent = "BROWSER_WEBGPU_UNAVAILABLE — install Chrome for Testing with WebGPU enabled";
    return;
  }
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x080b12);
  const camera = new THREE.PerspectiveCamera(60, canvasElement.width / canvasElement.height, 0.1, 100);
  camera.position.z = 3;
  const renderer = new WebGPURenderer({ canvas: canvasElement, antialias: false });
  await renderer.init();
  renderer.setSize(canvasElement.width, canvasElement.height, false);
  const cube = new THREE.Mesh(new THREE.BoxGeometry(), new THREE.MeshBasicMaterial({ color: 0x36d6ff, wireframe: true }));
  scene.add(cube);
  let frame = 0;
  function animate(): void {
    frame++;
    cube.rotation.x = frame * 0.01;
    cube.rotation.y = frame * 0.013;
    modeElement.textContent = `Browser WebGPU fallback — browser-authoritative simulation frame ${frame}`;
    renderer.render(scene, camera);
    requestAnimationFrame(animate);
  }
  animate();
  document.querySelector("#mirror")?.addEventListener("click", () => { modeElement.textContent = "MIRROR SPIKE — NOT THREE BACKEND — runtime launch is manual developer mode"; });
}
void main();
