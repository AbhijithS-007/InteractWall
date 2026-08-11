import { Code, Globe, HeartHandshake } from 'lucide-react';

export default function About() {
  return (
    <div style={{maxWidth: '600px', margin: '0 auto', textAlign: 'center', paddingTop: '2rem'}}>
      <div style={{marginBottom: '3rem'}}>
        <h1 style={{fontSize: '3rem', margin: '0 0 0.5rem 0', display: 'flex', justifyContent: 'center', alignItems: 'center', gap: '0.75rem'}}>
          <span className="brand-text">Interact<span className="brand-accent">Wall</span></span>
        </h1>
        <div style={{display: 'inline-block', padding: '0.25rem 1rem', background: 'rgba(0, 240, 255, 0.1)', color: 'var(--accent)', borderRadius: '20px', fontWeight: 600, fontSize: '0.9rem', border: '1px solid rgba(0, 240, 255, 0.2)'}}>
          Version 1.0.0
        </div>
      </div>

      <div className="card" style={{textAlign: 'left', marginBottom: '2rem'}}>
        <h3 style={{marginTop: 0, marginBottom: '1rem', color: 'white'}}>Our Philosophy</h3>
        <p style={{color: 'var(--text-secondary)', fontSize: '1.05rem', lineHeight: 1.6, margin: 0}}>
          <strong style={{color: 'var(--accent)'}}>Privacy first. Everything local.</strong><br/><br/>
          We believe your desktop is your personal space. InteractWall is built from the ground up to be a completely offline, high-performance engine. There are no accounts, no bundled telemetry, and no data leaving your machine. 
        </p>
      </div>

      <div style={{display: 'flex', gap: '1rem', justifyContent: 'center'}}>
        <button className="secondary" style={{display: 'flex', alignItems: 'center', gap: '0.5rem'}}>
          <Globe size={18} /> Website
        </button>
        <button className="secondary" style={{display: 'flex', alignItems: 'center', gap: '0.5rem'}}>
          <Code size={18} /> Source Code
        </button>
        <button className="secondary" style={{display: 'flex', alignItems: 'center', gap: '0.5rem'}}>
          <HeartHandshake size={18} /> Support Us
        </button>
      </div>
    </div>
  );
}
